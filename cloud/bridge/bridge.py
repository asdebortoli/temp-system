#!/usr/bin/env python3
"""
bridge.py — Ponte TMT (Chunk G).

Assina o broker MQTT (TLS), persiste telemetria/eventos no Supabase e envia alertas
ao Telegram. Também responde comandos (/start, /status) e vigia a ausência de
heartbeat (device offline). Tudo parametrizado por variáveis de ambiente (.env).

Fluxo:  MQTT (tmt/<id>/{telemetry,event,heartbeat})  →  Supabase  +  Telegram
"""
import os
import ssl
import json
import time
import logging
import threading
from collections import deque
from datetime import datetime, timezone

import requests
import paho.mqtt.client as mqtt
from dotenv import load_dotenv
from supabase import create_client

load_dotenv()

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)
log = logging.getLogger("bridge")

# --- Configuração (env) -----------------------------------------------------
MQTT_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.getenv("MQTT_PORT", "8883"))
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "tmt")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "tmt")
MQTT_CAFILE = os.getenv("MQTT_CAFILE", "../broker/certs/ca.crt")

SUPABASE_URL = os.getenv("SUPABASE_URL", "http://127.0.0.1:54321")
SUPABASE_KEY = os.getenv("SUPABASE_KEY", "")

TELEGRAM_BOT_TOKEN = os.getenv("TELEGRAM_BOT_TOKEN", "")
TELEGRAM_CHAT_ID = os.getenv("TELEGRAM_CHAT_ID", "")

HEARTBEAT_INTERVAL_S = int(os.getenv("HEARTBEAT_INTERVAL_S", "60"))
OFFLINE_AFTER_S = 2 * HEARTBEAT_INTERVAL_S  # RF07: sem mensagem por 2 períodos → offline

TG_API = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}"

# --- Estado em memória ------------------------------------------------------
sb = create_client(SUPABASE_URL, SUPABASE_KEY)
_seen_events = deque(maxlen=1000)      # dedup RN01: (device, type, ts, normalized)
_seen_set = set()
_last_seen = {}                        # device_id -> epoch da última mensagem
_online = {}                           # device_id -> bool (para alarme de offline)
_device_names = {}                     # device_id -> nome amigável (cache)
_running = True


# --- Telegram ---------------------------------------------------------------
def tg_send(text: str, chat_id: str | None = None):
    if not TELEGRAM_BOT_TOKEN or not (chat_id or TELEGRAM_CHAT_ID):
        log.warning("Telegram não configurado — mensagem não enviada: %s", text)
        return
    try:
        requests.post(
            f"{TG_API}/sendMessage",
            json={"chat_id": chat_id or TELEGRAM_CHAT_ID, "text": text},
            timeout=10,
        )
    except Exception as e:  # noqa: BLE001
        log.error("falha ao enviar Telegram: %s", e)


def device_name(device_id: str) -> str:
    if device_id in _device_names:
        return _device_names[device_id]
    try:
        res = sb.table("devices").select("name").eq("device_id", device_id).execute()
        name = (res.data[0]["name"] if res.data and res.data[0].get("name") else device_id)
    except Exception:  # noqa: BLE001
        name = device_id
    _device_names[device_id] = name
    return name


def hhmm(ts: int) -> str:
    return datetime.fromtimestamp(ts, tz=timezone.utc).strftime("%d/%m %H:%M UTC")


# --- Supabase ---------------------------------------------------------------
def upsert_device_seen(device_id: str):
    now = datetime.now(timezone.utc)
    _last_seen[device_id] = now.timestamp()
    try:
        sb.table("devices").upsert(
            {"device_id": device_id, "last_seen": now.isoformat()}
        ).execute()
    except Exception as e:  # noqa: BLE001
        log.error("upsert device falhou: %s", e)
    # Voltou a falar depois de offline?
    if _online.get(device_id) is False:
        tg_send(f"✅ {device_name(device_id)}: dispositivo voltou a se comunicar — {hhmm(int(now.timestamp()))}")
    _online[device_id] = True


def save_reading(device_id: str, p: dict):
    row = {
        "device_id": device_id,
        "ts": p.get("ts"),
        "temp_c": p.get("temp_c"),
        "hum_pct": p.get("hum_pct"),
        "light": p.get("light"),
        "mains_ok": p.get("mains_ok"),
    }
    try:
        sb.table("readings").insert(row).execute()
    except Exception as e:  # noqa: BLE001
        log.error("insert reading falhou: %s", e)


def save_event(device_id: str, p: dict):
    row = {
        "device_id": device_id,
        "ts": p.get("ts"),
        "type": p.get("type"),
        "state": p.get("state"),
        "severity": p.get("severity"),
        "value": p.get("value"),
        "threshold": p.get("threshold"),
        "normalized": bool(p.get("normalized", False)),
    }
    try:
        sb.table("events").insert(row).execute()
    except Exception as e:  # noqa: BLE001
        log.error("insert event falhou: %s", e)


# --- Regras de negócio ------------------------------------------------------
def event_is_new(device_id: str, p: dict) -> bool:
    """Dedup RN01: ignora reenvios (mesmo device/tipo/ts/normalized), p.ex. do buffer."""
    key = (device_id, p.get("type"), p.get("ts"), bool(p.get("normalized")))
    if key in _seen_set:
        return False
    _seen_events.append(key)
    _seen_set.add(key)
    while len(_seen_set) > len(_seen_events):
        # mantém _seen_set alinhado ao deque (remove os que saíram)
        _seen_set.intersection_update(_seen_events)
        break
    return True


def notify_event(device_id: str, p: dict):
    name = device_name(device_id)
    typ = p.get("type")
    ts = p.get("ts") or int(time.time())
    val = p.get("value")
    thr = p.get("threshold")

    if p.get("normalized"):
        tg_send(f"✅ {name}: retorno à normalidade "
                f"({typ}, {val} ) — {hhmm(ts)}")
        return

    if typ == "thermal":
        tg_send(f"🔥 {name}: TEMPERATURA fora da faixa: {val} °C "
                f"(limite {thr} °C) — {hhmm(ts)}")
    elif typ == "panic":
        tg_send(f"🚨 {name}: BOTÃO DE PÂNICO acionado! (emergência) — {hhmm(ts)}")
    elif typ == "power":
        tg_send(f"⚡ {name}: FALHA DE ENERGIA detectada — {hhmm(ts)}")
    elif typ == "humidity":
        tg_send(f"💧 {name}: UMIDADE fora da faixa: {val} %UR "
                f"(limite {thr}) — {hhmm(ts)}")
    elif typ == "door":
        tg_send(f"🚪 {name}: PORTA aberta tempo demais — {hhmm(ts)}")
    else:
        tg_send(f"⚠️ {name}: evento {typ} — {hhmm(ts)}")


# --- MQTT -------------------------------------------------------------------
def on_connect(client, userdata, flags, reason_code, properties=None):
    log.info("MQTT conectado (rc=%s) — assinando tmt/+/#", reason_code)
    client.subscribe("tmt/+/#", qos=1)


def on_message(client, userdata, msg):
    parts = msg.topic.split("/")
    if len(parts) < 3 or parts[0] != "tmt":
        return
    device_id, suffix = parts[1], parts[2]

    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except Exception:  # noqa: BLE001
        log.warning("payload inválido em %s: %r", msg.topic, msg.payload[:80])
        return

    upsert_device_seen(device_id)

    if suffix == "telemetry":
        save_reading(device_id, payload)
        log.info("telemetry %s temp=%s", device_id, payload.get("temp_c"))
    elif suffix == "event":
        if not event_is_new(device_id, payload):
            log.info("evento duplicado ignorado (%s)", payload.get("type"))
            return
        save_event(device_id, payload)
        notify_event(device_id, payload)
        log.info("event %s type=%s normalized=%s",
                 device_id, payload.get("type"), payload.get("normalized"))
    elif suffix == "heartbeat":
        log.info("heartbeat %s uptime=%ss", device_id, payload.get("uptime_s"))


# --- Watchdog de heartbeat (RF07) ------------------------------------------
def watchdog_loop():
    while _running:
        time.sleep(HEARTBEAT_INTERVAL_S)
        now = time.time()
        for device_id, last in list(_last_seen.items()):
            if _online.get(device_id) and (now - last) > OFFLINE_AFTER_S:
                _online[device_id] = False
                tg_send(f"🔴 {device_name(device_id)}: dispositivo OFFLINE "
                        f"(sem mensagens há {int(now - last)} s)")
                log.warning("device %s offline", device_id)


# --- Comandos do Telegram (/start, /status) --------------------------------
def latest_status(device_id: str) -> str:
    try:
        res = (sb.table("readings").select("*")
               .eq("device_id", device_id).order("ts", desc=True).limit(1).execute())
        if not res.data:
            return f"{device_name(device_id)}: sem leituras ainda."
        r = res.data[0]
        online = "online" if _online.get(device_id, False) else "offline"
        return (f"{device_name(device_id)} [{online}]: "
                f"{r.get('temp_c')} °C, {r.get('hum_pct')} %UR, "
                f"mains={'ok' if r.get('mains_ok') else 'falha'} — {hhmm(r.get('ts'))}")
    except Exception as e:  # noqa: BLE001
        return f"erro ao consultar status: {e}"


def handle_command(text: str, chat_id: str):
    text = (text or "").strip()
    if text.startswith("/start") or text.startswith("/help"):
        tg_send("TMT bot. Comandos:\n/status [device_id] — última leitura\n"
                "Sem argumento usa o device padrão (tmt-dev-01).", chat_id)
    elif text.startswith("/status"):
        args = text.split()
        device_id = args[1] if len(args) > 1 else "tmt-dev-01"
        tg_send(latest_status(device_id), chat_id)


def telegram_poll_loop():
    if not TELEGRAM_BOT_TOKEN:
        log.warning("sem TELEGRAM_BOT_TOKEN — polling de comandos desativado")
        return
    offset = None
    while _running:
        try:
            r = requests.get(f"{TG_API}/getUpdates",
                             params={"timeout": 25, "offset": offset}, timeout=30)
            for upd in r.json().get("result", []):
                offset = upd["update_id"] + 1
                msg = upd.get("message") or upd.get("edited_message") or {}
                chat = msg.get("chat", {})
                handle_command(msg.get("text", ""), str(chat.get("id", "")))
        except Exception as e:  # noqa: BLE001
            log.error("erro no polling do Telegram: %s", e)
            time.sleep(3)


# --- Main -------------------------------------------------------------------
def main():
    if not SUPABASE_KEY:
        log.error("SUPABASE_KEY vazio — configure o .env (veja .env.example)")

    # client_id fixo: o broker permite só UMA conexão com este id, então uma 2ª instância
    # da bridge derruba a 1ª em vez de entregar tudo em duplicado (mensagens repetidas no
    # Telegram). Evita o bug de duas bridges rodando ao mesmo tempo.
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="tmt-bridge")
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.tls_set(ca_certs=MQTT_CAFILE, tls_version=ssl.PROTOCOL_TLS_CLIENT)
    client.tls_insecure_set(True)   # CA validada; ignora o CN (broker local self-signed)
    client.on_connect = on_connect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=1, max_delay=30)

    threading.Thread(target=watchdog_loop, daemon=True).start()
    threading.Thread(target=telegram_poll_loop, daemon=True).start()

    log.info("conectando ao broker %s:%s (TLS)...", MQTT_HOST, MQTT_PORT)
    client.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    tg_send("🟢 Bridge TMT iniciada.")
    client.loop_forever()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        _running = False
        log.info("encerrando bridge")
