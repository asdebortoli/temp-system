/*
 * state_machine.h — Máquina de estados E1–E6 (PLAN §3).
 *
 * Chunk A: apenas o stub de inicialização. Lógica dos estados, dedup de eventos (RN01),
 * prioridade do pânico (RN02) e normalização (RF09) são implementados no Chunk C.
 */
#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

/* Cria/inicia a tarefa da máquina de estados. No-op no Chunk A. */
void state_machine_start(void);

#endif /* STATE_MACHINE_H */
