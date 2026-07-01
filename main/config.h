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
#define PIN_DS18B20          4              /* GPIO4  — sensor de temperatura (1-Wire) */
#define PIN_DHT22            5              /* GPIO5  — sensor de umidade (digital)     */
#define PIN_LDR_ADC          34             /* GPIO34 — LDR / detecção de porta (ADC1)  */
#define ADC_CHAN_LDR         ADC_CHANNEL_6  /* GPIO34 = ADC1 canal 6 (driver oneshot)   */
#define PIN_PANIC            15             /* GPIO15 — botão de pânico (IRQ)           */
#define PIN_MAINS_ADC        35             /* GPIO35 — sensor de rede elétrica (ADC1)  */
#define ADC_CHAN_MAINS       ADC_CHANNEL_7  /* GPIO35 = ADC1 canal 7 (driver oneshot)   */
#define PIN_BUZZER           13             /* GPIO13 — buzzer (sinalização local)      */

/* ---------------------------------------------------------------------------
 * Intervalos (segundos)
 * ------------------------------------------------------------------------- */
#define SAMPLE_INTERVAL_S    10             /* RF01: período de leitura dos sensores */
#define HEARTBEAT_INTERVAL_S 60             /* período de publicação do heartbeat    */

/* ---------------------------------------------------------------------------
 * Limiares / thresholds (RN03 — parametrizáveis)
 * Padrão: geladeira de alimentos (0–5 °C)
 * ------------------------------------------------------------------------- */
#define TEMP_MIN_C           0.0f           /* °C  — abaixo disso: alerta térmico */
#define TEMP_MAX_C           23.0f           /* °C  — acima disso:  alerta térmico */
#define HUM_MIN_PCT          30.0f          /* %UR — abaixo disso: alerta umidade */
#define HUM_MAX_PCT          70.0f          /* %UR — acima disso:  alerta umidade */
#define DOOR_LIGHT_THRESHOLD 2000           /* leitura ADC bruta do LDR: acima = porta aberta */
#define DOOR_OPEN_CRITICAL_S 60             /* segundos de porta aberta até alerta crítico    */
#define MAINS_ADC_THRESHOLD  1500           /* leitura ADC bruta da rede: abaixo = sem energia (RF06) */

/* ---------------------------------------------------------------------------
 * Aquisição (Chunk B)
 * ------------------------------------------------------------------------- */
#define PANIC_DEBOUNCE_MS    200            /* janela de debounce do botão de pânico (ISR)         */

/* Sensor de umidade: 11 = DHT11, 22 = DHT22/AM2302. Diferem no pulso de início (DHT11 ≥18 ms,
 * DHT22 ≥1 ms) e na codificação (DHT11 = inteiro.decimal; DHT22 = valor×10 em 16 bits). */
#define DHT_MODEL            11
#if DHT_MODEL == 11
#define DHT_START_LOW_US     20000          /* DHT11: linha baixa ≥18 ms */
#else
#define DHT_START_LOW_US     1200           /* DHT22: linha baixa ≥1 ms  */
#endif
#define DHT_BIT_THRESHOLD_US 50             /* pulso alto acima disso ⇒ bit '1' (igual nos dois)   */

/* ---------------------------------------------------------------------------
 * Contrato MQTT (PLAN §6) — tópico base: tmt/<device_id>/<sufixo>
 * ------------------------------------------------------------------------- */
#define MQTT_BASE_TOPIC      "tmt"
#define MQTT_TOPIC_TELEMETRY "telemetry"
#define MQTT_TOPIC_EVENT     "event"
#define MQTT_TOPIC_HEARTBEAT "heartbeat"
#define MQTT_TOPIC_CMD       "cmd"

#endif /* CONFIG_H */
