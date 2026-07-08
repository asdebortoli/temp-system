/*
 * sensors.h — Aquisição de sensores (DS18B20, pânico, buzzer).
 *
 * Chunk B: drivers reais + tarefa periódica de aquisição. A cada SAMPLE_INTERVAL_S a
 * tarefa empilha um sensor_reading_t na fila exposta por sensors_get_queue(); o botão de
 * pânico sinaliza um semáforo de alta prioridade (sensors_get_panic_sem()) independente do
 * período de amostragem. A máquina de estados (Chunk C) consome ambos.
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

/*
 * Amostra de sensores publicada na fila de aquisição (PLAN §6 — payload de telemetria).
 * Leitura inválida (falha do DS18B20) é marcada com NAN.
 */
typedef struct {
    int64_t ts;        /* epoch em segundos (time(NULL); ~0 até o SNTP do Chunk D) */
    float   temp_c;    /* °C  — NAN se a leitura do DS18B20 falhar  */
} sensor_reading_t;

/* Inicializa todos os drivers e cria as tarefas de aquisição e de pânico. */
void sensors_init(void);

/* Fila de sensor_reading_t (produzida pela tarefa de aquisição, consumida no Chunk C). */
QueueHandle_t sensors_get_queue(void);

/* Semáforo sinalizado pela ISR do botão de pânico (RN02 — prioridade máxima, Chunk C/E5). */
SemaphoreHandle_t sensors_get_panic_sem(void);

/* Buzzer de sinalização local (GPIO13). A política de acionamento é do Chunk C. */
void buzzer_on(void);
void buzzer_off(void);

#endif /* SENSORS_H */
