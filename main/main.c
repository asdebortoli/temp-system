/*
 * main.c — Ponto de entrada do firmware TMT (Monitoramento Térmico de Baixo Custo).
 *
 * Inicializa os subsistemas (armazenamento offline, sensores, rede, máquina de estados)
 * e cede o controle. No Chunk A todos são stubs no-op: o objetivo é apenas um binário
 * que compila e linka com a estrutura modular do PLAN §5.
 */
#include "esp_log.h"

#include "config.h"
#include "storage.h"
#include "sensors.h"
#include "net.h"
#include "state_machine.h"

static const char *TAG = "tmt";

void app_main(void)
{
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " TMT — Monitoramento Térmico  (fw %s)", FW_VERSION);
    ESP_LOGI(TAG, " device_id=%s  amostragem=%ds  heartbeat=%ds",
             DEVICE_ID, SAMPLE_INTERVAL_S, HEARTBEAT_INTERVAL_S);
    ESP_LOGI(TAG, "==============================================");

    storage_init();        /* buffer offline (NVS) — Chunk E */
    sensors_init();        /* aquisição de sensores — Chunk B */
    net_init();            /* Wi-Fi + MQTT/TLS — Chunk D       */
    state_machine_start(); /* lógica E1–E6 — Chunk C           */

    ESP_LOGI(TAG, "Inicialização concluída (esqueleto Chunk A).");
}
