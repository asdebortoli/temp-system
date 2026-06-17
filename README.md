# TMT — Sistema Inteligente de Monitoramento Térmico de Baixo Custo

Projeto de extensão (UNIVALI). Firmware ESP-IDF/FreeRTOS para **ESP32-WROOM-32 (DevKit V1)** que
monitora a temperatura, a umidade e o estado de ambientes refrigerados de pequenos
estabelecimentos (restaurantes, laboratórios), publicando telemetria e alertas em tempo real.

Acadêmicos: Alexandre Debortoli de Souza e Maria Julia Lamim Severino · Orientador: Felipe Viel

## Arquitetura

```
ESP32 (sensores → máquina de estados E1–E6 → MQTT/TLS)
   → HiveMQ Cloud (broker)
   → Bridge Python (paho + Telegram) → Supabase (Postgres)
   → Telegram (responsável)
```

O ESP32 lê os sensores, aplica a máquina de estados (E1 normal … E6 falha de energia) e publica
em um broker MQTT sobre TLS. Um serviço-ponte em Python ingere as mensagens, aplica regras de
negócio, persiste no Supabase e notifica o responsável via Telegram. Quando offline, o firmware
armazena as leituras em um buffer em NVS e retransmite ao reconectar.

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

> As mensagens de console e os comentários do código estão em **português**.
