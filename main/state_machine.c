/*
 * state_machine.c — Máquina de estados térmica + pânico (Chunk C).
 *
 * Consome, por um QueueSet, a fila de leituras (sensors_get_queue) e o semáforo do
 * botão de pânico (sensors_get_panic_sem). Para cada leitura publica a telemetria e
 * avalia a temperatura; o pânico é tratado imediatamente com prioridade máxima (RN02).
 *
 * Escopo do produto (monitor de geladeira): apenas TEMPERATURA (DS18B20) e PÂNICO.
 * Umidade, porta e energia foram removidas do produto.
 */
#include "state_machine.h"
#include "sensors.h"
#include "net.h"
#include "config.h"

#include <math.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "state_machine";

/* Estado de deduplicação térmica (RN01): um alerta ao sair da faixa, uma
 * normalização ao voltar. Sem isso, cada amostra fora da faixa geraria um evento. */
static bool s_thermal_active = false;

const char *event_type_str(event_type_t type)
{
    switch (type) {
        case EV_THERMAL:  return "thermal";
        case EV_PANIC:    return "panic";
        default:          return "unknown";
    }
}

/* Monta e publica um event_t para a nuvem (net cuida de buffer offline se preciso). */
static void emit_event(event_type_t type, const char *state, const char *severity,
                       float value, float threshold, bool normalized)
{
    event_t e = {
        .ts         = (int64_t)time(NULL),
        .type       = type,
        .state      = state,
        .severity   = severity,
        .value      = value,
        .threshold  = threshold,
        .normalized = normalized,
    };
    net_publish_event(&e);
}

/* E1/E2 — avalia a temperatura com dedup (RN01) e normalização (RF09). */
static void evaluate_thermal(const sensor_reading_t *r)
{
    if (isnan(r->temp_c)) {
        return;   /* leitura inválida do DS18B20: não muda de estado */
    }

    bool below = r->temp_c < TEMP_MIN_C;
    bool above = r->temp_c > TEMP_MAX_C;
    bool out_of_range = below || above;

    if (out_of_range && !s_thermal_active) {
        float thr = below ? TEMP_MIN_C : TEMP_MAX_C;
        ESP_LOGW(TAG, "E2 alerta térmico: %.2f °C fora de [%.1f, %.1f] °C",
                 r->temp_c, TEMP_MIN_C, TEMP_MAX_C);
        emit_event(EV_THERMAL, "E2", "CRIT", r->temp_c, thr, false);
        s_thermal_active = true;
    } else if (!out_of_range && s_thermal_active) {
        ESP_LOGI(TAG, "E1 normalização térmica: %.2f °C de volta à faixa", r->temp_c);
        emit_event(EV_THERMAL, "E1", "INFO", r->temp_c, NAN, true);
        s_thermal_active = false;
    }
}

/* Estado do pânico (E5): alterna a cada toque no botão (RN02). */
static bool s_panic_active = false;

/* E5 — pânico com TOGGLE, não-bloqueante (prioridade máxima, RN02):
 *   1º toque  → liga o buzzer + evento panic (E5/CRIT).
 *   2º toque  → desliga o buzzer + evento de retorno à normalidade (normalized=true),
 *               que a ponte traduz em "fim do pânico" no Telegram. */
static void handle_panic(void)
{
    s_panic_active = !s_panic_active;

    if (s_panic_active) {
        ESP_LOGW(TAG, "E5 PÂNICO — emergência acionada (RN02)");
        buzzer_on();
        emit_event(EV_PANIC, "E5", "CRIT", NAN, NAN, false);
    } else {
        ESP_LOGI(TAG, "E5 pânico encerrado — buzzer desligado");
        buzzer_off();
        emit_event(EV_PANIC, "E1", "INFO", NAN, NAN, true);
    }
}

static void state_machine_task(void *arg)
{
    QueueHandle_t     q     = sensors_get_queue();
    SemaphoreHandle_t panic = sensors_get_panic_sem();

    /* O QueueSet precisa comportar a soma dos comprimentos: fila de 8 + semáforo de 1. */
    QueueSetHandle_t set = xQueueCreateSet(8 + 1);
    xQueueAddToSet(q, set);
    xQueueAddToSet(panic, set);

    ESP_LOGI(TAG, "aguardando leituras e eventos de pânico...");

    for (;;) {
        QueueSetMemberHandle_t member = xQueueSelectFromSet(set, portMAX_DELAY);

        if (member == (QueueSetMemberHandle_t)panic) {
            xSemaphoreTake(panic, 0);
            handle_panic();                     /* RN02: pânico antes de tudo */
        } else if (member == (QueueSetMemberHandle_t)q) {
            sensor_reading_t r;
            if (xQueueReceive(q, &r, 0) == pdTRUE) {
                net_publish_telemetry(&r);      /* telemetria de toda leitura */
                evaluate_thermal(&r);           /* E1/E2 */
            }
        }
    }
}

void state_machine_start(void)
{
    if (sensors_get_queue() == NULL || sensors_get_panic_sem() == NULL) {
        ESP_LOGE(TAG, "fila/semáforo de sensores indisponível — FSM não iniciada");
        return;
    }
    xTaskCreate(state_machine_task, "state_machine", 4096, NULL, 6, NULL);
    ESP_LOGI(TAG, "máquina de estados iniciada (térmico E1/E2 + pânico E5)");
}
