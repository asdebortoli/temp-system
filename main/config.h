/*
 * config.h — Configuração central do firmware (TMT — Telemetria de Monitoramento Térmico)
 *
 * Todas as constantes parametrizáveis do produto: mapa de pinos (Quadro 3 / PLAN §2),
 * intervalos de amostragem, limiares (thresholds) e contrato de tópicos MQTT.
 *
 * RN03: os limiares são parametrizáveis — ajuste aqui para outro cenário (câmara fria,
 * ambiente, freezer). Os valores-padrão abaixo são para ARMAZENAMENTO REFRIGERADO DE
 * ALIMENTOS (geladeira, faixa ANVISA RDC 216/2004).
 *
 * Unidades (RNF09): temperatura em °C, umidade relativa em %UR.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ---------------------------------------------------------------------------
 * Identificação do dispositivo
 * ------------------------------------------------------------------------- */
#define DEVICE_ID            "tmt-dev-01"   /* placeholder — sobrescrever por dispositivo */
#define FW_VERSION           "0.1.0"

/* ---------------------------------------------------------------------------
 * Mapa de pinos (PLAN §2 / Quadro 3)
 * ------------------------------------------------------------------------- */
#define PIN_DS18B20          4              /* GPIO4  — sensor de temperatura (1-Wire)  */
#define PIN_PANIC            15             /* GPIO15 — botão de pânico (IRQ). ATENÇÃO:  */
                                            /*          strapping pin — o botão precisa  */
                                            /*          estar SOLTO no boot (nível alto). */
#define PIN_BUZZER           13             /* GPIO13 — buzzer PASSIVO via transistor NPN */
                                            /*          S8050; tom gerado por PWM (LEDC)  */

/* ---------------------------------------------------------------------------
 * Intervalos (segundos)
 * ------------------------------------------------------------------------- */
#define SAMPLE_INTERVAL_S    10             /* RF01: período de leitura dos sensores */
#define HEARTBEAT_INTERVAL_S 60             /* período de publicação do heartbeat    */

/* ---------------------------------------------------------------------------
 * Limiares / thresholds (RN03 — parametrizáveis)
 * Valor de TESTE DE BANCADA: com TEMP_MAX_C 28 °C a temperatura ambiente fica
 * "normal" e basta aquecer o sensor com a mão para cruzar o limite e disparar o
 * alerta térmico. Para geladeira de alimentos (produção), use TEMP_MAX_C 5.0f —
 * faixa 0–5 °C (ANVISA RDC 216/2004).
 * ------------------------------------------------------------------------- */
#define TEMP_MIN_C           0.0f           /* °C  — abaixo disso: alerta térmico */
#define TEMP_MAX_C           20.0f          /* °C  — acima disso:  alerta térmico */

/* ---------------------------------------------------------------------------
 * Aquisição (Chunk B)
 * ------------------------------------------------------------------------- */
#define PANIC_DEBOUNCE_MS    200            /* janela de debounce do botão de pânico (ISR)         */

/* Buzzer PASSIVO (sem oscilador próprio): o tom é gerado por PWM (LEDC) em um único GPIO,
 * que chaveia a base do transistor NPN — o buzzer enxerga uma onda quadrada de ~5 V (VIN).
 * A frequência abaixo é o tom audível; ajuste à ressonância do seu buzzer (2–4 kHz). */
#define BUZZER_FREQ_HZ       2700           /* Hz — frequência do tom (máx. volume na ressonância) */

/* ---------------------------------------------------------------------------
 * Contrato MQTT (PLAN §6) — tópico base: tmt/<device_id>/<sufixo>
 * ------------------------------------------------------------------------- */
#define MQTT_BASE_TOPIC      "tmt"
#define MQTT_TOPIC_TELEMETRY "telemetry"
#define MQTT_TOPIC_EVENT     "event"
#define MQTT_TOPIC_HEARTBEAT "heartbeat"
#define MQTT_TOPIC_CMD       "cmd"

/* ---------------------------------------------------------------------------
 * Rede (Chunk D) — Wi-Fi + MQTT/TLS
 * As credenciais sensíveis ficam em secrets.h (NÃO versionado): copie
 * main/secrets.h.example para main/secrets.h e preencha WIFI_* e MQTT_*.
 * MQTT_HOST = IP do Mac na LAN onde roda o broker Mosquitto em Docker.
 * ------------------------------------------------------------------------- */
#include "secrets.h"           /* WIFI_SSID, WIFI_PASS, MQTT_HOST, MQTT_USER, MQTT_PASS */
#define MQTT_PORT            8883          /* MQTT sobre TLS (RNF04)               */
#define SNTP_SERVER          "pool.ntp.org"

/* ---------------------------------------------------------------------------
 * Buffer offline (Chunk E) — ring buffer na partição NVS "buffer"
 * ------------------------------------------------------------------------- */
#define BUFFER_PARTITION_NAME    "buffer"
#define BUFFER_CAPACITY_RECORDS  200       /* nº máx. de registros; ao encher sobrescreve o mais antigo */
#define HEAP_LOG_INTERVAL_S      300       /* período do log de heap livre (endurecimento RNF07)         */

#endif /* CONFIG_H */
