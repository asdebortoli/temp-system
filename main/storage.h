/*
 * storage.h — Buffer offline em NVS (PLAN §5, RF08).
 *
 * Ring buffer FIFO na partição NVS "buffer": armazena mensagens (tópico + JSON)
 * quando o dispositivo está offline e as retransmite ao reconectar. A implementação
 * é do Chunk E; as declarações ficam aqui para a camada de rede (Chunk D) integrar.
 */
#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

/* Tamanhos máximos de um registro bufferizado (tópico e payload JSON). */
#define STORAGE_TOPIC_MAX  96
#define STORAGE_JSON_MAX   512

/* Inicializa o armazenamento offline (partição NVS "buffer"). */
void storage_init(void);

/* Enfileira uma mensagem (tópico + JSON) no ring buffer. Ao encher, descarta a
 * mais antiga (oldest-overwrite). Retorna ESP_OK em caso de sucesso. */
esp_err_t storage_push(const char *topic, const char *json);

/* Copia (sem remover) o registro mais antigo. Retorna false se o buffer estiver vazio. */
bool storage_peek_oldest(char *topic_out, size_t topic_sz, char *json_out, size_t json_sz);

/* Remove o registro mais antigo (chamar após retransmiti-lo com sucesso). */
void storage_drop_oldest(void);

/* Número de registros atualmente bufferizados. */
size_t storage_count(void);

#endif /* STORAGE_H */
