# Matriz de requisitos — TMT (monitor de geladeira)

Legenda: ✅ atendido/verificado · ⏳ implementado, verificação em hardware pendente ·
➖ fora do escopo do produto (removido).

Escopo do produto: **temperatura (DS18B20) + botão de pânico + buzzer**. Umidade, porta e falha
de energia foram **removidas** (hardware e código). Broker e Supabase locais em Docker; bridge no
host. Validado na placa passo a passo pela série de correção **R1–R6** (`tasks/ChunkR1.md`…`R6.md`):
buzzer é passivo (tom PWM via transistor NPN) e o pânico é toggle (liga/desliga a cada toque).

## Funcionais (RF)

| Req | Descrição | Status | Evidência |
|-----|-----------|--------|-----------|
| RF01 | Leitura periódica dos sensores | ✅ | `acquisition_task` a cada `SAMPLE_INTERVAL_S`; temperatura na placa (R2) chega em `readings` (R6) |
| RF02 | Alerta térmico | ✅ | E2 em `state_machine.c`; um alerta ao sair da faixa (R5) → evento `thermal` + Telegram (R6) |
| RF03 | Alerta de umidade | ➖ | removido do produto (escopo geladeira) |
| RF04 | Porta aberta tempo demais | ➖ | removido do produto (escopo geladeira) |
| RF05 | Pânico + buzzer | ✅ | E5 toggle (R4) + buzzer passivo tom PWM (R3) validados na placa; `panic` → Telegram (R6) |
| RF06 | Alerta de falta de energia | ➖ | removido do produto (escopo geladeira) |
| RF07 | Heartbeat + alarme de ausência | ✅ | `heartbeat_task` (firmware) + `watchdog_loop` no bridge (offline após 2× intervalo) |
| RF08 | Buffer offline + retransmissão | ✅ | ring buffer NVS (`storage.c`) encheu offline e drenou no reconnect (`net.c`) na placa (R6) |
| RF09 | Normalização (retorno à faixa) | ✅ | uma normalização ao voltar à faixa (R5) → evento `normalized=true` + Telegram (R6) |
| RF10 | Comando `/status` | ⏳ | implementado no bridge; testar com bot real |
| RF11 | Persistência + export CSV | ✅ | linhas em `readings`/`events` (sim.); export em `cloud/SUPABASE.md` |

## Não-funcionais (RNF)

| Req | Descrição | Status | Evidência |
|-----|-----------|--------|-----------|
| RNF01 | Deduplicação (1 alerta por condição) | ✅ | flag `thermal_active` (firmware) + dedup no bridge; duplicado ignorado (sim.) |
| RNF02 | Prioridade do pânico | ✅ | `QueueSet` trata o semáforo de pânico imediatamente (RN02) |
| RNF03 | Limiares parametrizáveis | ✅ | `config.h` (`TEMP_MIN_C`/`TEMP_MAX_C` etc.) |
| RNF04 | Wi-Fi + MQTT sobre TLS | ✅ | firmware `mqtts://…:8883` com CA embutida; broker TLS; bridge TLS (conexão rc=Success) |
| RNF05 | Latência detecção→alerta < 30 s | ✅ | publish→insert+formatação Telegram em < 1 s (sim.) |
| RNF06 | Autonomia de bateria ≥ 10 min | ➖ | hardware |
| RNF07 | Soak 24 h + heap estável | ⏳ | `heap_monitor_task` + task watchdog idle ativos; soak de 24 h não executado |
| RNF08 | Retenção ≥ 12 meses | ✅ | função `tmt_purge_old()` (agendar via `pg_cron`) — `cloud/SUPABASE.md` |
| RNF09 | Unidades documentadas (°C/%UR) | ✅ | `config.h` e README |

## Regras de negócio (RN)

| Req | Descrição | Status | Evidência |
|-----|-----------|--------|-----------|
| RN01 | Dedup de eventos | ✅ | verificado (firmware + bridge) |
| RN02 | Pânico imediato, severidade máxima | ✅ | E5 `severity=CRIT`, tratado antes das leituras |
| RN03 | Parametrização de limiares | ✅ | `config.h` |
| RN04 | Export CSV para log de qualidade | ✅ | `cloud/SUPABASE.md` |
| RN05 | Postura LGPD | ✅ | `cloud/SUPABASE.md` (minimização, titularidade, exclusão em cascata) |
