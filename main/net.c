/*
 * net.c — Conectividade: Wi-Fi STA + SNTP + cliente MQTT sobre TLS (Chunk D).
 *
 * Fluxo: conecta ao Wi-Fi (credenciais em secrets.h) → ao obter IP sincroniza o
 * relógio por SNTP e abre o cliente esp-mqtt em mqtts://MQTT_HOST:8883, validando o
 * broker pela CA embutida (certs/broker_ca.pem). Publica telemetria/eventos/heartbeat
 * como JSON (cJSON). Quando offline, delega ao buffer NVS (Chunk E) e, ao reconectar,
 * retransmite o que ficou pendente (RF08).
 */
#include "net.h"
#include "config.h"
#include "sensors.h"
#include "state_machine.h"
#include "storage.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"

static const char *TAG = "net";

/* Certificado da CA do broker, embutido pelo CMake (EMBED_TXTFILES). */
extern const uint8_t broker_ca_pem_start[] asm("_binary_broker_ca_pem_start");

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t       s_net_events;
static esp_mqtt_client_handle_t s_mqtt;
static volatile bool            s_mqtt_online  = false;
static bool                     s_sntp_started = false;
static bool                     s_mqtt_started = false;
static int                      s_wifi_retry   = 0;

/* ---------------------------------------------------------------------------
 * Buffer offline: retransmissão ao (re)conectar (RF08)
 * ------------------------------------------------------------------------- */
static void drain_buffer(void)
{
    char topic[STORAGE_TOPIC_MAX];
    char json[STORAGE_JSON_MAX];
    int  drained = 0;

    while (s_mqtt_online && s_mqtt && storage_count() > 0) {
        if (!storage_peek_oldest(topic, sizeof(topic), json, sizeof(json))) {
            break;
        }
        int msg_id = esp_mqtt_client_publish(s_mqtt, topic, json, 0, 1, 0);
        if (msg_id < 0) {
            break;   /* fila do cliente cheia / desconectou: tenta de novo depois */
        }
        storage_drop_oldest();
        drained++;
    }
    if (drained > 0) {
        ESP_LOGI(TAG, "buffer offline: %d registro(s) retransmitido(s)", drained);
    }
}

/* ---------------------------------------------------------------------------
 * Publicação genérica: publica se online, senão bufferiza (Chunk E)
 * ------------------------------------------------------------------------- */
static void make_topic(char *out, size_t n, const char *suffix)
{
    snprintf(out, n, "%s/%s/%s", MQTT_BASE_TOPIC, DEVICE_ID, suffix);
}

static void publish_or_buffer(const char *suffix, const char *json, int qos)
{
    char topic[STORAGE_TOPIC_MAX];
    make_topic(topic, sizeof(topic), suffix);

    if (s_mqtt_online && s_mqtt) {
        int msg_id = esp_mqtt_client_publish(s_mqtt, topic, json, 0, qos, 0);
        if (msg_id >= 0) {
            return;
        }
        ESP_LOGW(TAG, "publish falhou em %s — bufferizando", topic);
    }
    storage_push(topic, json);
}

void net_publish_telemetry(const sensor_reading_t *r)
{
    cJSON *j = cJSON_CreateObject();
    if (!j) return;

    cJSON_AddNumberToObject(j, "ts", (double)r->ts);
    if (isnan(r->temp_c)) cJSON_AddNullToObject(j, "temp_c");
    else                  cJSON_AddNumberToObject(j, "temp_c", r->temp_c);

    char *s = cJSON_PrintUnformatted(j);
    if (s) {
        publish_or_buffer(MQTT_TOPIC_TELEMETRY, s, 0);
        cJSON_free(s);
    }
    cJSON_Delete(j);
}

void net_publish_event(const event_t *e)
{
    cJSON *j = cJSON_CreateObject();
    if (!j) return;

    cJSON_AddNumberToObject(j, "ts", (double)e->ts);
    cJSON_AddStringToObject(j, "type", event_type_str(e->type));
    cJSON_AddStringToObject(j, "state", e->state ? e->state : "");
    cJSON_AddStringToObject(j, "severity", e->severity ? e->severity : "");
    if (isnan(e->value)) cJSON_AddNullToObject(j, "value");
    else                 cJSON_AddNumberToObject(j, "value", e->value);
    if (isnan(e->threshold)) cJSON_AddNullToObject(j, "threshold");
    else                     cJSON_AddNumberToObject(j, "threshold", e->threshold);
    cJSON_AddBoolToObject(j, "normalized", e->normalized);

    char *s = cJSON_PrintUnformatted(j);
    if (s) {
        publish_or_buffer(MQTT_TOPIC_EVENT, s, 1);   /* eventos: QoS 1 (entrega importa) */
        cJSON_free(s);
    }
    cJSON_Delete(j);
}

bool net_is_online(void)
{
    return s_mqtt_online;
}

/* ---------------------------------------------------------------------------
 * Heartbeat periódico (PLAN §6)
 * ------------------------------------------------------------------------- */
static void heartbeat_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_S * 1000));

        cJSON *j = cJSON_CreateObject();
        if (!j) continue;
        cJSON_AddNumberToObject(j, "ts", (double)time(NULL));
        cJSON_AddNumberToObject(j, "uptime_s", (double)(esp_timer_get_time() / 1000000));
        cJSON_AddStringToObject(j, "fw", FW_VERSION);

        char *s = cJSON_PrintUnformatted(j);
        if (s) {
            publish_or_buffer(MQTT_TOPIC_HEARTBEAT, s, 0);
            cJSON_free(s);
        }
        cJSON_Delete(j);
    }
}

/* ---------------------------------------------------------------------------
 * MQTT
 * ------------------------------------------------------------------------- */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_mqtt_online = true;
            ESP_LOGI(TAG, "MQTT conectado ao broker (TLS) — retransmitindo buffer");
            drain_buffer();
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_mqtt_online = false;
            ESP_LOGW(TAG, "MQTT desconectado");
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT erro de transporte/TLS");
            break;
        default:
            break;
    }
}

static void mqtt_start(void)
{
    static char uri[64];
    snprintf(uri, sizeof(uri), "mqtts://%s:%d", MQTT_HOST, MQTT_PORT);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .broker.verification.certificate = (const char *)broker_ca_pem_start,
        .broker.verification.skip_cert_common_name_check = true,   /* CA self-signed local */
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
        .session.keepalive = 30,
    };

    s_mqtt = esp_mqtt_client_init(&cfg);
    if (!s_mqtt) {
        ESP_LOGE(TAG, "falha ao criar cliente MQTT");
        return;
    }
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt);
    ESP_LOGI(TAG, "cliente MQTT iniciado (%s)", uri);
}

/* ---------------------------------------------------------------------------
 * Wi-Fi + SNTP
 * ------------------------------------------------------------------------- */
static void start_sntp(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(SNTP_SERVER);
    esp_netif_sntp_init(&cfg);
    ESP_LOGI(TAG, "SNTP iniciado (%s)", SNTP_SERVER);
}

static void net_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_mqtt_online = false;
        xEventGroupClearBits(s_net_events, WIFI_CONNECTED_BIT);
        s_wifi_retry++;
        ESP_LOGW(TAG, "Wi-Fi desconectado — reconectando (tentativa %d)", s_wifi_retry);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_wifi_retry = 0;
        xEventGroupSetBits(s_net_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi conectado — IP: " IPSTR, IP2STR(&event->ip_info.ip));

        if (!s_sntp_started) { start_sntp(); s_sntp_started = true; }
        if (!s_mqtt_started) { mqtt_start();  s_mqtt_started = true; }
    }
}

void net_init(void)
{
    /* NVS padrão: exigido pelo driver Wi-Fi (armazena calibração/PHY). */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    s_net_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, net_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, net_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(heartbeat_task, "heartbeat", 3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "net_init: Wi-Fi STA (SSID=%s), broker mqtts://%s:%d",
             WIFI_SSID, MQTT_HOST, MQTT_PORT);
}
