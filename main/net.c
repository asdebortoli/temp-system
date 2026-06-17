/*
 * net.c — Stub de conectividade (Chunk A).
 * Wi-Fi + MQTT/TLS real: Chunk D.
 */
#include "net.h"
#include "esp_log.h"

static const char *TAG = "net";

void net_init(void)
{
    /* TODO Chunk D: conectar Wi-Fi, sincronizar relógio via SNTP, abrir cliente
     * MQTT sobre TLS (HiveMQ Cloud) e publicar telemetry/event/heartbeat. */
    ESP_LOGI(TAG, "net_init (stub) — sem conexão de rede ainda");
}
