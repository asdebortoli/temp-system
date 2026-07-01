/*
 * net.h — Conectividade: Wi-Fi + cliente MQTT/TLS (PLAN §6, RNF04).
 *
 * Expõe a inicialização da pilha de rede e as funções de publicação usadas pela
 * máquina de estados (Chunk C). A implementação (Wi-Fi STA, SNTP, esp-mqtt sobre TLS,
 * heartbeat e integração com o buffer offline) é do Chunk D.
 */
#ifndef NET_H
#define NET_H

#include <stdbool.h>

#include "sensors.h"        /* sensor_reading_t */
#include "state_machine.h"  /* event_t          */

/* Inicializa Wi-Fi + SNTP + cliente MQTT/TLS e a tarefa de heartbeat. */
void net_init(void);

/* true quando o cliente MQTT está conectado ao broker (usado pelo buffer offline). */
bool net_is_online(void);

/* Publica uma leitura como telemetria (tmt/<id>/telemetry). Offline → buffer (Chunk E). */
void net_publish_telemetry(const sensor_reading_t *r);

/* Publica um evento (tmt/<id>/event). Offline → buffer (Chunk E). */
void net_publish_event(const event_t *e);

#endif /* NET_H */
