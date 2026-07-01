/*
 * sensors.c — Drivers de sensores e tarefa de aquisição (Chunk B).
 *
 * Peripherais (Quadro 3 / PLAN §2):
 *   - DS18B20 (GPIO4, 1-Wire) via componentes gerenciados espressif/onewire_bus + ds18b20.
 *   - DHT22   (GPIO5) — driver próprio via periférico RMT: o pulso de início é dado pelo GPIO
 *     e os 40 bits da resposta são capturados em hardware (RMT RX), imune a jitter de
 *     interrupções/Wi-Fi. Sem componente externo (decisão do PLAN §9).
 *   - LDR     (GPIO34, ADC1) e rede elétrica (GPIO35, ADC1) — driver ADC oneshot.
 *   - Botão de pânico (GPIO15) — IRQ + semáforo de alta prioridade (RN02).
 *   - Buzzer  (GPIO13) — saída digital.
 *
 * A tarefa de aquisição lê todos os sensores a cada SAMPLE_INTERVAL_S e empilha um
 * sensor_reading_t na fila. Falhas de leitura são marcadas (NAN / -1) e registradas em log,
 * sem travar o laço (RNF). Nenhuma decisão de alerta aqui — isso é do Chunk C.
 */
#include "sensors.h"

#include <math.h>
#include <time.h>

#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "esp_adc/adc_oneshot.h"

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
static adc_oneshot_unit_handle_t s_adc;

static rmt_channel_handle_t s_dht_rmt;          /* canal RMT RX para o DHT22 (GPIO5) */
static QueueHandle_t s_dht_rx_queue;            /* recebe o resultado da captura (do ISR) */
static rmt_symbol_word_t s_dht_symbols[64];     /* buffer de símbolos capturados */

static bool s_ds18b20_ok;                       /* drivers que inicializaram OK    */
static bool s_dht22_ok;
static bool s_adc_ok;

static volatile int64_t s_last_panic_us;        /* debounce do botão de pânico     */

/* ---------------------------------------------------------------------------
 * DS18B20 (temperatura, 1-Wire — GPIO4)
 * ------------------------------------------------------------------------- */
static esp_err_t ds18b20_init(void)
{
    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = PIN_DS18B20,
        .flags = { .en_pull_up = 1 },           /* pull-up externo ainda recomendado */
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
 * DHT22 (umidade — GPIO5) — captura via RMT RX
 * ------------------------------------------------------------------------- */

/* Callback do RMT (contexto de ISR): repassa o resultado da captura para a fila. */
static bool dht_rx_done(rmt_channel_handle_t chan,
                        const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t hpw = pdFALSE;
    xQueueSendFromISR(s_dht_rx_queue, edata, &hpw);
    return hpw == pdTRUE;
}

/*
 * Autoteste do pino de dados (DIAGNÓSTICO TEMPORÁRIO): antes do RMT assumir o pino, dirige-o
 * alto/baixo e lê de volta, e mede o nível em repouso com pull-up. Interpretação:
 *   drive_hi deve ser 1 e drive_lo deve ser 0  → senão o GPIO de saída está danificado;
 *   idle_pullup deve ser 1 (pull-up levanta a linha solta) → se for 0, a linha está em curto
 *   com o GND ou mal ligada (ex.: DATA no trilho errado).
 */
static void dht_pin_selftest(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_DHT22,
        .mode = GPIO_MODE_INPUT_OUTPUT,     /* dirige e lê o pino ao mesmo tempo */
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    gpio_set_level(PIN_DHT22, 1);
    esp_rom_delay_us(50);
    int drive_hi = gpio_get_level(PIN_DHT22);

    gpio_set_level(PIN_DHT22, 0);
    esp_rom_delay_us(50);
    int drive_lo = gpio_get_level(PIN_DHT22);

    gpio_set_direction(PIN_DHT22, GPIO_MODE_INPUT);   /* pull-up continua ativo */
    esp_rom_delay_us(50);
    int idle_pullup = gpio_get_level(PIN_DHT22);

    ESP_LOGW(TAG, "DHT pin selftest GPIO%d: drive_hi=%d drive_lo=%d idle_pullup=%d",
             PIN_DHT22, drive_hi, drive_lo, idle_pullup);
}

static esp_err_t dht22_init(void)
{
    dht_pin_selftest();                     /* DIAGNÓSTICO — remover depois */

    rmt_rx_channel_config_t rx_cfg = {
        .gpio_num = PIN_DHT22,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,           /* 1 tick = 1 µs */
        .mem_block_symbols = 64,
    };
    esp_err_t err = rmt_new_rx_channel(&rx_cfg, &s_dht_rmt);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DHT22: falha ao criar canal RMT RX (%s)", esp_err_to_name(err));
        return err;
    }

    s_dht_rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    if (s_dht_rx_queue == NULL) {
        ESP_LOGW(TAG, "DHT22: falha ao criar fila de recepção");
        return ESP_ERR_NO_MEM;
    }

    rmt_rx_event_callbacks_t cbs = { .on_recv_done = dht_rx_done };
    rmt_rx_register_event_callbacks(s_dht_rmt, &cbs, NULL);
    err = rmt_enable(s_dht_rmt);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DHT22: falha ao habilitar RMT (%s)", esp_err_to_name(err));
        return err;
    }

    /* Pull-up interno como reforço ao externo (após o RMT configurar o pino). */
    gpio_set_pull_mode(PIN_DHT22, GPIO_PULLUP_ONLY);
    return ESP_OK;
}

/*
 * Uma tentativa de leitura do sensor DHT (11 ou 22). Preenche *hum (%UR) e *temp (°C). Retorna
 * true em sucesso. O pulso de início é dado pelo GPIO; a resposta (handshake de 80/80 µs +
 * 40 bits) é capturada pelo RMT. Cada bit = 50 µs baixos + pulso alto curto (~27 µs = '0') ou
 * longo (~70 µs = '1'); o bit é decidido pela largura da parte alta de cada símbolo.
 */
static bool dht22_read_once(float *hum, float *temp)
{
    /* Pulso de início via GPIO: baixa (≥18 ms no DHT11, ≥1 ms no DHT22), sobe, e libera. */
    gpio_set_direction(PIN_DHT22, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_DHT22, 0);
    esp_rom_delay_us(DHT_START_LOW_US);
    gpio_set_level(PIN_DHT22, 1);
    esp_rom_delay_us(30);
    gpio_set_direction(PIN_DHT22, GPIO_MODE_INPUT);

    /* Arma a captura logo após soltar a linha. */
    rmt_receive_config_t rx_cfg = {
        .signal_range_min_ns = 1000,        /* ignora glitch < 1 µs */
        .signal_range_max_ns = 200000,      /* 200 µs de linha parada encerra a recepção */
    };
    if (rmt_receive(s_dht_rmt, s_dht_symbols, sizeof(s_dht_symbols), &rx_cfg) != ESP_OK) {
        return false;
    }

    rmt_rx_done_event_data_t evt;
    if (xQueueReceive(s_dht_rx_queue, &evt, pdMS_TO_TICKS(20)) != pdTRUE) {
        ESP_LOGW(TAG, "DHT22 DIAG: timeout RMT — nenhuma borda capturada (sensor não respondeu)");
        return false;
    }

    /* DIAGNÓSTICO TEMPORÁRIO: mostra o que o RMT capturou (remover depois de calibrar). */
    ESP_LOGW(TAG, "DHT22 DIAG: nsym=%u", (unsigned)evt.num_symbols);
    int n = evt.num_symbols < 8 ? evt.num_symbols : 8;
    for (int k = 0; k < n; k++) {
        ESP_LOGW(TAG, "  sym[%d]: L0=%u d0=%u us | L1=%u d1=%u us", k,
                 evt.received_symbols[k].level0, evt.received_symbols[k].duration0,
                 evt.received_symbols[k].level1, evt.received_symbols[k].duration1);
    }

    /*
     * Layout esperado: símbolo 0 = handshake do sensor (baixa ~80 µs + alta ~80 µs), depois
     * 40 símbolos de dados (baixa ~50 µs + alta curta/longa). Como armamos a captura antes da
     * resposta, o handshake costuma ser o 1º símbolo → pulamos 1 quando há ≥41 símbolos.
     * Em cada bit, a parte ALTA (duration1) define o valor: longa ⇒ '1'.
     */
    if (evt.num_symbols < 40) {
        ESP_LOGD(TAG, "DHT22: símbolos insuficientes (%u)", (unsigned)evt.num_symbols);
        return false;
    }
    int offset = (evt.num_symbols >= 41) ? 1 : 0;
    const rmt_symbol_word_t *bits = &evt.received_symbols[offset];

    uint8_t data[5] = {0};
    for (int i = 0; i < 40; i++) {
        if (bits[i].duration1 > DHT_BIT_THRESHOLD_US) {     /* parte alta longa ⇒ bit '1' */
            data[i / 8] |= (1 << (7 - (i % 8)));
        }
    }

    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        ESP_LOGD(TAG, "DHT: checksum inválido");
        return false;
    }

#if DHT_MODEL == 11
    /* DHT11: byte inteiro + byte decimal (os decimais costumam ser 0). */
    *hum = data[0] + data[1] * 0.1f;
    *temp = data[2] + data[3] * 0.1f;
#else
    /* DHT22: umidade e temperatura em 16 bits (valor×10); bit de sinal na temperatura. */
    *hum = ((data[0] << 8) | data[1]) / 10.0f;
    int16_t raw_t = ((data[2] & 0x7F) << 8) | data[3];
    *temp = raw_t / 10.0f;
    if (data[2] & 0x80) {
        *temp = -*temp;
    }
#endif
    return true;
}

/* Leitura com retentativas: o DHT22 é instável; tenta algumas vezes antes de desistir. */
static bool dht22_read(float *hum, float *temp)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        if (dht22_read_once(hum, temp)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(50));      /* o sensor precisa de folga entre tentativas */
    }
    ESP_LOGW(TAG, "DHT22: sem resposta após 3 tentativas (verifique fiação e o sensor)");
    return false;
}

/* ---------------------------------------------------------------------------
 * ADC oneshot (LDR — GPIO34, rede elétrica — GPIO35)
 * ------------------------------------------------------------------------- */
static esp_err_t adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC: falha ao criar unidade (%s)", esp_err_to_name(err));
        return err;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,               /* faixa completa ~0–3.3 V */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(s_adc, ADC_CHAN_LDR, &chan_cfg);
    adc_oneshot_config_channel(s_adc, ADC_CHAN_MAINS, &chan_cfg);
    return ESP_OK;
}

/* Leitura ADC bruta de um canal; retorna -1 em falha. */
static int adc_read_raw(adc_channel_t ch)
{
    if (!s_adc_ok) {
        return -1;
    }
    int raw = 0;
    if (adc_oneshot_read(s_adc, ch, &raw) != ESP_OK) {
        return -1;
    }
    return raw;
}

/* ---------------------------------------------------------------------------
 * Buzzer (GPIO13)
 * ------------------------------------------------------------------------- */
static void buzzer_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BUZZER,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(PIN_BUZZER, 0);
}

void buzzer_on(void)  { gpio_set_level(PIN_BUZZER, 1); }
void buzzer_off(void) { gpio_set_level(PIN_BUZZER, 0); }

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

/*
 * Tarefa de pânico (alta prioridade): registra o acionamento em sub-segundo, independente do
 * tick de amostragem. No Chunk B serve para validar o caminho de IRQ; no Chunk C a máquina de
 * estados passará a consumir o mesmo semáforo (E5, prioridade máxima — RN02).
 */
static void panic_task(void *arg)
{
    for (;;) {
        if (xSemaphoreTake(s_panic_sem, portMAX_DELAY) == pdTRUE) {
            ESP_LOGW(TAG, "BOTÃO DE PÂNICO acionado (RN02 — prioridade máxima)");
        }
    }
}

/* ---------------------------------------------------------------------------
 * Tarefa de aquisição
 * ------------------------------------------------------------------------- */
static void acquisition_task(void *arg)
{
    /* DHT22 precisa de ≥1 s após energização antes de responder. */
    vTaskDelay(pdMS_TO_TICKS(2000));

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        sensor_reading_t r = {0};
        r.ts = (int64_t)time(NULL);             /* ~0 até o SNTP do Chunk D */

        float t;
        r.temp_c = (s_ds18b20_ok && ds18b20_read(&t) == ESP_OK) ? t : NAN;

        float h, dht_t;
        r.hum_pct = (s_dht22_ok && dht22_read(&h, &dht_t)) ? h : NAN;

        r.light = adc_read_raw(ADC_CHAN_LDR);
        int mains_raw = adc_read_raw(ADC_CHAN_MAINS);
        r.mains_ok = (mains_raw >= 0) && (mains_raw >= MAINS_ADC_THRESHOLD);

        ESP_LOGI(TAG, "leitura: temp=%.2f °C  hum=%.1f %%UR  light=%d  mains=%s",
                 r.temp_c, r.hum_pct, r.light, r.mains_ok ? "ok" : "FALHA");

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

    /* Cada init é tolerante a falha: um sensor ausente registra aviso e segue. */
    s_ds18b20_ok = (ds18b20_init() == ESP_OK);
    s_dht22_ok = (dht22_init() == ESP_OK);
    s_adc_ok = (adc_init() == ESP_OK);
    buzzer_init();
    panic_init();

    ESP_LOGI(TAG, "drivers: ds18b20=%s dht(rmt)=%s adc=%s buzzer=ok panico=ok",
             s_ds18b20_ok ? "ok" : "FALHA", s_dht22_ok ? "ok" : "FALHA",
             s_adc_ok ? "ok" : "FALHA");

    xTaskCreate(panic_task, "panic", 2048, NULL, 10, NULL);
    xTaskCreate(acquisition_task, "acq", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "aquisição iniciada (intervalo=%ds)", SAMPLE_INTERVAL_S);
}
