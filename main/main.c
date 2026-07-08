/*
 * main.c — Ponto de entrada do firmware TMT (Monitoramento Térmico de Baixo Custo).
 *
 * Inicializa os subsistemas (buffer offline, rede, sensores, máquina de estados) e
 * sobe uma tarefa leve de monitoramento de heap (endurecimento RNF07). O fluxo de
 * dados é: aquisição (Chunk B) → máquina de estados (Chunk C) → rede/MQTT (Chunk D),
 * com buffer NVS (Chunk E) quando offline.
 */
#include "esp_log.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "storage.h"
#include "sensors.h"
#include "net.h"
#include "state_machine.h"

static const char *TAG = "tmt";

/* Loga o heap livre periodicamente para flagrar vazamento em execução longa (RNF07).
 * O task watchdog do FreeRTOS (idle) já está ativo por padrão para pegar travamentos. */
static void heap_monitor_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(HEAP_LOG_INTERVAL_S * 1000));
        ESP_LOGI(TAG, "heap livre: %u bytes (mínimo histórico: %u)",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)esp_get_minimum_free_heap_size());
    }
}

void app_main(void)
{
    /* Silencia os dumps verbosos do driver Wi-Fi/PHY e da configuração de GPIO, que
     * poluíam o console a cada boot/leitura sem valor de diagnóstico no dia a dia. */
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("phy_init", ESP_LOG_WARN);
    esp_log_level_set("gpio", ESP_LOG_WARN);
    /* O driver RMT do 1-Wire loga um ERRO "hw buffer too small" a cada leitura do DS18B20
     * (inerente ao RMT do ESP32 clássico, sem DMA). A leitura ainda completa e passa no CRC
     * — temperatura válida —, então silenciamos totalmente a tag "rmt". Falha real do sensor
     * aparece como temp=nan e erros da tag "ds18b20", que continuam visíveis. */
    esp_log_level_set("rmt", ESP_LOG_NONE);

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " TMT — Monitoramento Térmico  (fw %s)", FW_VERSION);
    ESP_LOGI(TAG, " device_id=%s  amostragem=%ds  heartbeat=%ds",
             DEVICE_ID, SAMPLE_INTERVAL_S, HEARTBEAT_INTERVAL_S);
    ESP_LOGI(TAG, " pinos: ds18b20=GPIO%d  panico=GPIO%d  buzzer=GPIO%d",
             PIN_DS18B20, PIN_PANIC, PIN_BUZZER);
    ESP_LOGI(TAG, " faixa térmica: [%.1f, %.1f] °C", TEMP_MIN_C, TEMP_MAX_C);
    ESP_LOGI(TAG, "==============================================");

    storage_init();        /* buffer offline (NVS) — Chunk E   */
    net_init();            /* Wi-Fi + SNTP + MQTT/TLS — Chunk D */
    sensors_init();        /* aquisição de sensores — Chunk B   */
    state_machine_start(); /* lógica E1/E2 + pânico — Chunk C   */

    xTaskCreate(heap_monitor_task, "heap_mon", 2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "Inicialização concluída.");
}
