/*
 * state_machine.h — Máquina de estados térmica + pânico (PLAN §3).
 *
 * Escopo do produto (monitor de geladeira): E1 NORMAL, E2 alerta térmico (com
 * normalização RF09 e dedup RN01) e E5 emergência/pânico (prioridade máxima RN02 +
 * buzzer). Umidade, porta e falha de energia foram removidas do produto.
 *
 * A tarefa consome a fila de sensor_reading_t e o semáforo de pânico via um QueueSet,
 * e publica event_t pela camada de rede (net_publish_event).
 */
#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>
#include <stdbool.h>

/* Tipo do evento (PLAN §6 — campo "type"). */
typedef enum {
    EV_THERMAL,   /* alerta/normalização de temperatura (E1/E2) */
    EV_PANIC,     /* botão de pânico (E5)                         */
} event_type_t;

/*
 * Evento estruturado publicado para a nuvem (PLAN §6 — tópico .../event).
 * Os ponteiros de string apontam para literais estáticos (não precisam de free).
 */
typedef struct {
    int64_t      ts;          /* epoch em segundos (time(NULL))                 */
    event_type_t type;        /* tipo do evento                                 */
    const char  *state;       /* estado da FSM: "E1", "E2", "E5"...             */
    const char  *severity;    /* "INFO" | "WARN" | "CRIT"                        */
    float        value;       /* valor sensoriado (NAN quando não se aplica)     */
    float        threshold;   /* limite violado (NAN quando não se aplica)       */
    bool         normalized;  /* true = retorno à faixa normal (RF09)            */
} event_t;

/* Nome curto do tipo do evento para o JSON MQTT ("thermal", "panic"...). */
const char *event_type_str(event_type_t type);

/* Cria/inicia a tarefa da máquina de estados. */
void state_machine_start(void);

#endif /* STATE_MACHINE_H */
