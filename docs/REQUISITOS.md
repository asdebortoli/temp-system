# Matriz de requisitos — TMT (demo de temperatura)

Legenda: ✅ atendido/verificado · ⏳ implementado, verificação em hardware pendente ·
➖ fora do escopo desta demo (sensor não validado ou hardware específico).

Escopo desta entrega: fluxo ponta a ponta **apenas para temperatura (DS18B20)**. Broker e
Supabase locais em Docker; bridge no host. Evidências marcadas "(sim.)" foram obtidas simulando
o ESP32 com `mosquitto_pub` (o firmware é gravado pelo usuário na placa).

## Funcionais (RF)

| Req | Descrição | Status | Evidência |
|-----|-----------|--------|-----------|
| RF01 | Leitura periódica dos sensores | ✅ | `acquisition_task` a cada `SAMPLE_INTERVAL_S`; telemetria inserida em `readings` (sim.) |
| RF02 | Alerta térmico | ✅ | E2 em `state_machine.c`; evento `thermal` inserido e alerta Telegram formatado (sim.) |
| RF03 | Alerta de umidade | ➖ | DHT não validado — E4 desativado |
| RF04 | Porta aberta tempo demais | ➖ | LDR não validado — E3 desativado |
| RF05 | Pânico + buzzer | ⏳ | E5 + `buzzer_on()` implementados; depende do botão/buzzer em HW |
| RF06 | Alerta de falta de energia | ➖ | sensor de rede não validado — E6 desativado |
| RF07 | Heartbeat + alarme de ausência | ✅ | `heartbeat_task` (firmware) + `watchdog_loop` no bridge (offline após 2× intervalo) |
| RF08 | Buffer offline + retransmissão | ⏳ | ring buffer NVS (`storage.c`) + dreno no reconnect (`net.c`); teste em HW pendente |
| RF09 | Normalização (retorno à faixa) | ✅ | evento `normalized=true` inserido + mensagem de retorno (sim.) |
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
