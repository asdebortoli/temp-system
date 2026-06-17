/*
 * sensors.c — Stub de aquisição de sensores (Chunk A).
 * Implementação real dos drivers: Chunk B.
 */
#include "sensors.h"
#include "esp_log.h"

static const char *TAG = "sensors";

void sensors_init(void)
{
    /* TODO Chunk B: inicializar DS18B20 (1-Wire), DHT22, ADCs (LDR, rede),
     * IRQ do botão de pânico e o buzzer; criar a tarefa de aquisição. */
    ESP_LOGI(TAG, "sensors_init (stub) — sem leitura real ainda");
}
