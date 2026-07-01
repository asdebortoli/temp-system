/*
 * storage.c — Ring buffer FIFO em NVS para operação offline (Chunk E, RF08).
 *
 * Guarda mensagens (tópico + JSON) na partição NVS "buffer" quando o dispositivo
 * está sem broker, e as devolve em ordem para retransmissão ao reconectar. Índices
 * (head/count) são persistidos para sobreviver a reset/queda de energia. Ao encher,
 * sobrescreve o registro mais antigo (oldest-overwrite). Protegido por mutex, pois
 * é acessado pela tarefa de publicação, pelo dreno e pelo heartbeat.
 */
#include "storage.h"
#include "config.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "storage";

#define NVS_NAMESPACE  "ringbuf"
#define KEY_HEAD       "head"
#define KEY_COUNT      "count"

/* Um registro cabe em "topic\njson\0". */
#define RECORD_MAX     (STORAGE_TOPIC_MAX + STORAGE_JSON_MAX + 2)

static nvs_handle_t     s_nvs;
static bool             s_ready;
static uint32_t         s_head;   /* índice do registro mais antigo   */
static uint32_t         s_count;  /* nº de registros ocupados          */
static SemaphoreHandle_t s_mutex;

static void slot_key(char *out, size_t n, uint32_t idx)
{
    snprintf(out, n, "r%03u", (unsigned)(idx % BUFFER_CAPACITY_RECORDS));
}

static void persist_meta(void)
{
    nvs_set_u32(s_nvs, KEY_HEAD, s_head);
    nvs_set_u32(s_nvs, KEY_COUNT, s_count);
    nvs_commit(s_nvs);
}

void storage_init(void)
{
    s_mutex = xSemaphoreCreateMutex();

    esp_err_t err = nvs_flash_init_partition(BUFFER_PARTITION_NAME);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "partição '%s' precisa de erase — recriando", BUFFER_PARTITION_NAME);
        nvs_flash_erase_partition(BUFFER_PARTITION_NAME);
        err = nvs_flash_init_partition(BUFFER_PARTITION_NAME);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "falha ao iniciar NVS '%s' (%s) — buffer offline desativado",
                 BUFFER_PARTITION_NAME, esp_err_to_name(err));
        return;
    }

    err = nvs_open_from_partition(BUFFER_PARTITION_NAME, NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "falha ao abrir namespace (%s) — buffer offline desativado",
                 esp_err_to_name(err));
        return;
    }

    if (nvs_get_u32(s_nvs, KEY_HEAD, &s_head) != ESP_OK)  s_head = 0;
    if (nvs_get_u32(s_nvs, KEY_COUNT, &s_count) != ESP_OK) s_count = 0;
    s_ready = true;

    ESP_LOGI(TAG, "buffer offline pronto (capacidade=%d, pendentes=%u)",
             BUFFER_CAPACITY_RECORDS, (unsigned)s_count);
}

esp_err_t storage_push(const char *topic, const char *json)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    char record[RECORD_MAX];
    int len = snprintf(record, sizeof(record), "%s\n%s", topic, json);
    if (len <= 0 || len >= (int)sizeof(record)) {
        ESP_LOGW(TAG, "registro grande demais — descartado");
        return ESP_ERR_INVALID_SIZE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Índice do próximo slot livre (== head quando cheio → sobrescreve o mais antigo). */
    uint32_t write_idx = (s_head + s_count) % BUFFER_CAPACITY_RECORDS;

    char key[8];
    slot_key(key, sizeof(key), write_idx);
    esp_err_t err = nvs_set_blob(s_nvs, key, record, len + 1);
    if (err == ESP_OK) {
        if (s_count == BUFFER_CAPACITY_RECORDS) {
            s_head = (s_head + 1) % BUFFER_CAPACITY_RECORDS;   /* descarta o mais antigo */
            ESP_LOGW(TAG, "buffer cheio — sobrescrevendo registro mais antigo");
        } else {
            s_count++;
        }
        persist_meta();
    } else {
        ESP_LOGW(TAG, "falha ao gravar registro (%s)", esp_err_to_name(err));
    }

    xSemaphoreGive(s_mutex);
    return err;
}

bool storage_peek_oldest(char *topic_out, size_t topic_sz, char *json_out, size_t json_sz)
{
    if (!s_ready) return false;

    bool ok = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_count > 0) {
        char key[8];
        slot_key(key, sizeof(key), s_head);
        char record[RECORD_MAX];
        size_t rlen = sizeof(record);
        if (nvs_get_blob(s_nvs, key, record, &rlen) == ESP_OK) {
            record[rlen ? rlen - 1 : 0] = '\0';           /* garante terminação */
            char *sep = strchr(record, '\n');
            if (sep) {
                *sep = '\0';
                snprintf(topic_out, topic_sz, "%s", record);
                snprintf(json_out, json_sz, "%s", sep + 1);
                ok = true;
            }
        }
    }

    xSemaphoreGive(s_mutex);
    return ok;
}

void storage_drop_oldest(void)
{
    if (!s_ready) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_count > 0) {
        s_head = (s_head + 1) % BUFFER_CAPACITY_RECORDS;
        s_count--;
        persist_meta();
    }
    xSemaphoreGive(s_mutex);
}

size_t storage_count(void)
{
    if (!s_ready) return 0;
    size_t n;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    n = s_count;
    xSemaphoreGive(s_mutex);
    return n;
}
