/*
 * storage.c — Stub do buffer offline (Chunk A).
 * Ring buffer em NVS real: Chunk E (partição "buffer").
 */
#include "storage.h"
#include "esp_log.h"

static const char *TAG = "storage";

void storage_init(void)
{
    /* TODO Chunk E: abrir a partição NVS "buffer" e implementar o ring buffer de
     * leituras/eventos (armazenar offline, retransmitir ao reconectar — RF08). */
    ESP_LOGI(TAG, "storage_init (stub) — sem buffer offline ainda");
}
