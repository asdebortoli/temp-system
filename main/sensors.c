/*
 * sensors.c — Drivers de sensores e tarefa de aquisição (Chunk B).
 *
 * Periféricos (PLAN §2):
 *   - DS18B20 (GPIO4, 1-Wire) via componentes gerenciados espressif/onewire_bus + ds18b20.
 *   - Botão de pânico (GPIO15) — IRQ + semáforo de alta prioridade (RN02).
 *   - Buzzer passivo (GPIO13) — tom por PWM (LEDC) chaveando um transistor NPN.
 *
 * A tarefa de aquisição lê a temperatura a cada SAMPLE_INTERVAL_S e empilha um
 * sensor_reading_t na fila. Falha de leitura é marcada (NAN) e registrada em log, sem travar
 * o laço (RNF). Nenhuma decisão de alerta aqui — isso é do Chunk C.
 */
#include "sensors.h"

#include <math.h>
#include <time.h>

#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"      /* usado só no auto-teste de diagnóstico do buzzer */

#include "onewire_bus.h"
#include "ds18b20.h"

#include "config.h"

static const char *TAG = "sensors";

/* ---------------------------------------------------------------------------
 * Estado do módulo
 * ------------------------------------------------------------------------- */
static QueueHandle_t        s_queue;            /* fila de sensor_reading_t        */
static SemaphoreHandle_t    s_panic_sem;        /* sinal do botão de pânico        */

static onewire_bus_handle_t s_owbus;
static ds18b20_device_handle_t s_ds18b20;

static bool s_ds18b20_ok;                       /* driver que inicializou OK       */

static volatile int64_t s_last_panic_us;        /* debounce do botão de pânico     */

/* ---------------------------------------------------------------------------
 * DS18B20 (temperatura, 1-Wire — GPIO4)
 * ------------------------------------------------------------------------- */
static esp_err_t ds18b20_init(void)
{
    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = PIN_DS18B20,
        /* O pull-up correto do 1-Wire é o resistor EXTERNO de 4.7 kΩ (DATA↔3V3). Mantemos
         * também o pull-up interno ligado (~45 kΩ, em paralelo) como rede de segurança: se o
         * externo estiver ausente/ruim, o interno ainda pode segurar a linha o suficiente
         * para o sensor responder. Com o externo instalado corretamente, o interno é
         * inofensivo (paralelo ≈ 4,2 kΩ). "reset bus error" = linha não sobe → cheque o 4.7 kΩ. */
        .flags = { .en_pull_up = 1 },
    };
    onewire_bus_rmt_config_t rmt_cfg = { .max_rx_bytes = 64 };
    esp_err_t err = onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &s_owbus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20: falha ao criar barramento 1-Wire (%s)", esp_err_to_name(err));
        return err;
    }

    ds18b20_config_t ds_cfg = {};
    err = ds18b20_new_single_device(s_owbus, &ds_cfg, &s_ds18b20);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20: dispositivo não encontrado (%s)", esp_err_to_name(err));
        return err;
    }
    ds18b20_set_resolution(s_ds18b20, DS18B20_RESOLUTION_12B);   /* 12 bits, ±0.5 °C (RNF03) */
    return ESP_OK;
}

/* Lê a temperatura em °C. Retorna ESP_OK e preenche *out, ou um erro em caso de falha. */
static esp_err_t ds18b20_read(float *out)
{
    esp_err_t err = ds18b20_trigger_temperature_conversion(s_ds18b20);  /* ~750 ms (bloqueante) */
    if (err != ESP_OK) {
        return err;
    }
    return ds18b20_get_temperature(s_ds18b20, out);
}

/* ---------------------------------------------------------------------------
 * Buzzer PASSIVO (GPIO13) — tom por PWM (LEDC) via transistor NPN (S8050)
 *
 * Buzzer passivo não tem oscilador interno: precisa de uma onda quadrada para soar. O LEDC
 * gera o tom (BUZZER_FREQ_HZ) num único GPIO13 → resistor 1 kΩ → base do NPN; emissor →
 * GND; coletor → buzzer; outra perna do buzzer → 5 V (VIN). O transistor chaveia os ~5 V,
 * então o buzzer soa bem mais alto do que ligado direto no GPIO (3,3 V).
 *   - buzzer_on()  = duty 50% → tom.   - buzzer_off() = duty 0 → mudo (pino em 0 V).
 * ------------------------------------------------------------------------- */
#define BUZZER_TIMER      LEDC_TIMER_0
#define BUZZER_MODE       LEDC_LOW_SPEED_MODE
#define BUZZER_CH         LEDC_CHANNEL_0
#define BUZZER_DUTY_HALF  512               /* 50% de 1024 (10 bits) */

static void buzzer_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = BUZZER_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = BUZZER_TIMER,
        .freq_hz         = BUZZER_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num   = PIN_BUZZER,
        .speed_mode = BUZZER_MODE,
        .channel    = BUZZER_CH,
        .timer_sel  = BUZZER_TIMER,
        .duty       = 0,                    /* começa mudo */
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
}

void buzzer_on(void)
{
    ledc_set_duty(BUZZER_MODE, BUZZER_CH, BUZZER_DUTY_HALF);
    ledc_update_duty(BUZZER_MODE, BUZZER_CH);
}

void buzzer_off(void)
{
    ledc_set_duty(BUZZER_MODE, BUZZER_CH, 0);
    ledc_update_duty(BUZZER_MODE, BUZZER_CH);
}

/* ---------------------------------------------------------------------------
 * Botão de pânico (GPIO15) — IRQ
 * ------------------------------------------------------------------------- */
static void panic_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_panic_us < (int64_t)PANIC_DEBOUNCE_MS * 1000) {
        return;                                 /* debounce */
    }
    s_last_panic_us = now;

    BaseType_t hpw = pdFALSE;
    xSemaphoreGiveFromISR(s_panic_sem, &hpw);
    if (hpw) {
        portYIELD_FROM_ISR();
    }
}

static void panic_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_PANIC,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,         /* botão para GND: borda de descida */
    };
    gpio_config(&cfg);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_PANIC, panic_isr, NULL);
}

/* ---------------------------------------------------------------------------
 * Tarefa de aquisição
 * ------------------------------------------------------------------------- */
static void acquisition_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        sensor_reading_t r = {0};
        r.ts = (int64_t)time(NULL);             /* ~0 até o SNTP do Chunk D */

        float t;
        r.temp_c = (s_ds18b20_ok && ds18b20_read(&t) == ESP_OK) ? t : NAN;

        ESP_LOGI(TAG, "leitura: temp=%.2f °C", r.temp_c);

        if (xQueueSend(s_queue, &r, 0) != pdTRUE) {
            ESP_LOGW(TAG, "fila de leituras cheia — amostra descartada");
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SAMPLE_INTERVAL_S * 1000));
    }
}

/* ---------------------------------------------------------------------------
 * Inicialização pública
 * ------------------------------------------------------------------------- */
QueueHandle_t sensors_get_queue(void)         { return s_queue; }
SemaphoreHandle_t sensors_get_panic_sem(void) { return s_panic_sem; }

void sensors_init(void)
{
    s_queue = xQueueCreate(8, sizeof(sensor_reading_t));
    s_panic_sem = xSemaphoreCreateBinary();
    if (s_queue == NULL || s_panic_sem == NULL) {
        ESP_LOGE(TAG, "falha ao alocar fila/semáforo — aquisição não iniciada");
        return;
    }

    /* O init do DS18B20 é tolerante a falha: sensor ausente registra aviso e segue. */
    s_ds18b20_ok = (ds18b20_init() == ESP_OK);
    buzzer_init();
    panic_init();

    /* Auto-teste: 2 bipes curtos no boot para validar a fiação do buzzer/transistor. */
    ESP_LOGI(TAG, "auto-teste do buzzer: 2 bipes em GPIO%d", PIN_BUZZER);
    for (int i = 0; i < 2; i++) {
        buzzer_on();  vTaskDelay(pdMS_TO_TICKS(120));
        buzzer_off(); vTaskDelay(pdMS_TO_TICKS(120));
    }

    ESP_LOGI(TAG, "drivers: ds18b20=%s buzzer=ok panico=ok",
             s_ds18b20_ok ? "ok" : "FALHA");

    /* O semáforo de pânico é consumido exclusivamente pela FSM (QueueSet, Chunk C). */
    xTaskCreate(acquisition_task, "acq", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "aquisição iniciada (intervalo=%ds)", SAMPLE_INTERVAL_S);
}
