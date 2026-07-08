# TMT — Sistema Inteligente de Monitoramento Térmico de Baixo Custo

Projeto de extensão (UNIVALI). Firmware ESP-IDF/FreeRTOS para **ESP32-WROOM-32 (DevKit V1)** que
monitora a **temperatura** de ambientes refrigerados (geladeiras) de pequenos estabelecimentos
(restaurantes, laboratórios), com **botão de pânico** e **buzzer**, publicando telemetria e
alertas em tempo real.

Acadêmicos: Alexandre Debortoli de Souza e Maria Julia Lamim Severino · Orientador: Felipe Viel

## Arquitetura

```
ESP32 (DS18B20 → máquina de estados E1/E2/E5 → MQTT/TLS)
   → Mosquitto (broker, TLS)
   → Bridge Python (paho → Supabase → Telegram)
   → Supabase (Postgres)  +  Telegram (responsável)
```

O ESP32 lê a temperatura, aplica a máquina de estados — **E1** normal, **E2** alerta térmico e
**E5** pânico — e publica em um broker MQTT sobre TLS. Um serviço-ponte em Python ingere as
mensagens, aplica regras de negócio, persiste no Supabase e notifica o responsável via Telegram.
Quando offline, o firmware armazena as leituras em um buffer em NVS e retransmite ao reconectar.

> **Escopo (monitor de geladeira):** o produto usa **um sensor de temperatura (DS18B20)**, um
> **botão de pânico** (toggle: liga/desliga o buzzer a cada toque) e um **buzzer passivo** (tom
> por PWM via transistor NPN). Umidade, porta e falha de energia foram removidas do produto. O
> fluxo foi validado na placa ponta a ponta pela série de correção **R1–R6** (`tasks/`). Broker e
> Supabase rodam **localmente em Docker** e o bridge roda no host — veja [`cloud/`](cloud/).

Visão completa e requisitos em **[`PLAN.md`](PLAN.md)** (§10 registra o histórico A–H → R1–R6).
A série de correção que definiu o produto atual está em **[`tasks/`](tasks/)**
(`ChunkR1.md` … `ChunkR6.md`), executada e testada na placa em ordem R1 → R6.

## Mapa de pinos

| Componente           | GPIO   | Função                                                |
|----------------------|--------|-------------------------------------------------------|
| DS18B20 (temperatura)| GPIO4  | Temperatura (1-Wire; pull-up externo 4.7 kΩ ↔ 3V3)    |
| Botão de pânico      | GPIO15 | Emergência (IRQ, INPUT_PULLUP; strapping — solto no boot) |
| Buzzer passivo       | GPIO13 | Sinalização sonora (tom PWM via transistor NPN S8050) |

Alimentação por USB; o buzzer é alimentado pelos 5 V do pino VIN (o GPIO só chaveia a base do
transistor). Diodo 1N4007 em paralelo com o buzzer (cátodo para o +5 V).

## Estrutura do firmware (`main/`)

| Arquivo                 | Responsabilidade                                      | Chunk |
|-------------------------|-------------------------------------------------------|-------|
| `main.c`                | `app_main`: inicialização e criação das tarefas       | A     |
| `config.h`              | Pinos, limiares, intervalos, tópicos MQTT (RN03)      | A     |
| `sensors.{c,h}`         | Driver do DS18B20, botão de pânico e buzzer + aquisição| B     |
| `state_machine.{c,h}`   | Lógica dos estados E1/E2/E5, dedup e normalização     | C     |
| `net.{c,h}`             | Wi-Fi + cliente MQTT/TLS                               | D     |
| `storage.{c,h}`         | Buffer offline em NVS (partição `buffer`)             | E     |

Apenas uma partição de aplicação (`factory`); a partição de dados `buffer` (NVS) guarda o buffer
offline. Veja `partitions.csv`.

## Como compilar e rodar

Requer o ambiente ESP-IDF (`idf.py` no PATH, `IDF_PATH` definido). Há um `.devcontainer`
(ESP-IDF + QEMU) para VS Code / Codespaces.

```sh
idf.py set-target esp32   # apenas se for reconfigurar o alvo
idf.py reconfigure        # após alterar idf_component.yml / partições
idf.py build              # compilar
idf.py flash monitor      # gravar na placa e ver a saída serial (Ctrl+] para sair)
```

Antes de compilar, crie o `main/secrets.h` com as credenciais (não versionado):

```sh
cp main/secrets.h.example main/secrets.h
# preencha: WIFI_SSID/WIFI_PASS e MQTT_HOST = IP do Mac na LAN (ex.: 192.168.100.178)
```

O firmware embute a CA do broker em `main/certs/broker_ca.pem` (gerada por `cloud/broker/gen-certs.sh`).

> As mensagens de console e os comentários do código estão em **português**.

## Stack local (demo) — broker + Supabase + bridge

Tudo roda no Mac (Docker + Python). Passo a passo:

```sh
# 1) Broker MQTT (Mosquitto, TLS)
cd cloud/broker && ./gen-certs.sh && docker compose up -d

# 2) Supabase local (Postgres + Studio)
cd ../supabase && supabase start        # Studio em http://127.0.0.1:54333

# 3) Bridge (MQTT → Supabase → Telegram)
cd ../bridge && python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env                     # preencha SUPABASE_KEY e o Telegram
python bridge.py
```

- Broker: [`cloud/broker/`](cloud/broker) — [`gen-certs.sh`](cloud/broker/gen-certs.sh) gera CA/cert/senha.
- Banco: [`cloud/SUPABASE.md`](cloud/SUPABASE.md) — schema, export CSV (RN04), retenção (RNF08), LGPD (RN05).
- Bridge: [`cloud/bridge/README.md`](cloud/bridge/README.md) — variáveis e comandos do bot.

Sem hardware, dá para simular o ESP32 publicando no broker (o container tem `mosquitto_pub`):

```sh
docker exec tmt-mosquitto mosquitto_pub -h localhost -p 8883 \
  --cafile /mosquitto/certs/ca.crt --insecure -u tmt -P tmt \
  -t tmt/tmt-dev-01/event \
  -m '{"ts":1782943560,"type":"thermal","state":"E2","severity":"CRIT","value":25.0,"threshold":23.0,"normalized":false}'
```

O checklist de requisitos (RF/RNF/RN) está em [`docs/REQUISITOS.md`](docs/REQUISITOS.md).
