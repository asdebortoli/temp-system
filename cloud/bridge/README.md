# Bridge TMT (Chunk G)

Serviço Python que liga o broker MQTT ao Supabase e ao Telegram:

```
MQTT (tmt/<id>/{telemetry,event,heartbeat}) → Supabase (readings/events) + Telegram
```

Faz: persistência das leituras/eventos, alertas no Telegram (com dedup RN01),
watchdog de heartbeat (device offline, RF07) e comandos `/start` e `/status`.

## Rodar (local, no Mac)

Pré-requisitos: broker no ar (`cloud/broker`) e Supabase no ar (`cloud/supabase`).

```bash
cd cloud/bridge
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env          # preencha SUPABASE_KEY e as credenciais do Telegram
python bridge.py
```

## Variáveis (.env)

| Variável | Descrição |
|----------|-----------|
| `MQTT_HOST/PORT/USERNAME/PASSWORD` | broker Mosquitto local (TLS, 8883) |
| `MQTT_CAFILE` | CA do broker (`../broker/certs/ca.crt`) |
| `SUPABASE_URL` / `SUPABASE_KEY` | API local (`supabase status`) + service_role key |
| `TELEGRAM_BOT_TOKEN` / `TELEGRAM_CHAT_ID` | bot do @BotFather + seu chat_id |
| `HEARTBEAT_INTERVAL_S` | deve casar com o firmware (`config.h`) |

## Comandos do bot

- `/start` ou `/help` — ajuda
- `/status [device_id]` — última leitura (padrão `tmt-dev-01`)

## Notas

- TLS valida o broker pela CA; o CN é ignorado (`tls_insecure_set(True)`) porque o
  certificado local é self-signed — espelha o `skip_cert_common_name_check` do firmware.
- Sem `TELEGRAM_BOT_TOKEN` o bridge ainda roda e persiste no Supabase; só não envia
  mensagens (útil para testar o caminho do banco antes de configurar o Telegram).
