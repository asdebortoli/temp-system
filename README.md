# TMT — Sistema Inteligente de Monitoramento Térmico de Baixo Custo

Projeto de extensão (UNIVALI). Firmware ESP-IDF/FreeRTOS para **ESP32-WROOM-32 (DevKit V1)** que
monitora a temperatura, a umidade e o estado de ambientes refrigerados de pequenos
estabelecimentos (restaurantes, laboratórios), publicando telemetria e alertas em tempo real.

Acadêmicos: Alexandre Debortoli de Souza e Maria Julia Lamim Severino · Orientador: Felipe Viel

## Arquitetura

```
ESP32 (sensores → máquina de estados E1–E6 → MQTT/TLS)
   → Mosquitto (broker, TLS)
   → Bridge Python (paho → Supabase → Telegram)
   → Supabase (Postgres)  +  Telegram (responsável)
```

O ESP32 lê os sensores, aplica a máquina de estados (E1 normal … E6 falha de energia) e publica
em um broker MQTT sobre TLS. Um serviço-ponte em Python ingere as mensagens, aplica regras de
negócio, persiste no Supabase e notifica o responsável via Telegram. Quando offline, o firmware
armazena as leituras em um buffer em NVS e retransmite ao reconectar.

> **Estado atual (demo de temperatura):** o pipeline está funcional ponta a ponta para o sensor
> **DS18B20** (temperatura), único validado em hardware. A máquina de estados avalia alerta
> térmico (E2), normalização (RF09) e pânico (E5); umidade/porta/energia estão desativados no
> firmware até que seus sensores sejam validados. Broker e Supabase rodam **localmente em Docker**
> e o bridge roda no host — veja [`cloud/`](cloud/).

Visão completa, requisitos e divisão do trabalho em **[`PLAN.md`](PLAN.md)**. O trabalho é
dividido em chunks independentes em **[`tasks/`](tasks/)** (`ChunkA.md` … `ChunkH.md`),
executados em ordem A → H.

## Mapa de pinos (Quadro 3)

| Componente           | GPIO   | Função                              |
|----------------------|--------|-------------------------------------|
| DS18B20 (temperatura)| GPIO4  | Temperatura interna (1-Wire)        |
| DHT22 (umidade)      | GPIO5  | Umidade relativa (digital)          |
| LDR (luz)            | GPIO34 | Detecção de porta aberta (ADC1)     |
| Botão de pânico      | GPIO15 | Acionamento de emergência (IRQ)     |
| Sensor de rede       | GPIO35 | Detecção de falta de energia (ADC1) |
| Buzzer               | GPIO13 | Sinalização sonora local            |

## Estrutura do firmware (`main/`)

| Arquivo                 | Responsabilidade                                      | Chunk |
|-------------------------|-------------------------------------------------------|-------|
| `main.c`                | `app_main`: inicialização e criação das tarefas       | A     |
| `config.h`              | Pinos, limiares, intervalos, tópicos MQTT (RN03)      | A     |
| `sensors.{c,h}`         | Drivers e aquisição dos sensores                      | B     |
| `state_machine.{c,h}`   | Lógica dos estados E1–E6, dedup e normalização        | C     |
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
