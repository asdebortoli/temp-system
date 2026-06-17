/*
 * state_machine.c — Stub da máquina de estados (Chunk A).
 * Lógica E1–E6 real: Chunk C.
 */
#include "state_machine.h"
#include "esp_log.h"

static const char *TAG = "state_machine";

void state_machine_start(void)
{
    /* TODO Chunk C: implementar E1 NORMAL, E2 alerta térmico, E3 porta aberta,
     * E4 alerta de umidade, E5 emergência (pânico, prioridade máxima — RN02),
     * E6 falha de energia; consumir fila de leituras, emitir eventos. */
    ESP_LOGI(TAG, "state_machine_start (stub) — sem lógica de estados ainda");
}
