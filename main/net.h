/*
 * net.h — Conectividade: Wi-Fi + cliente MQTT/TLS (PLAN §6, RNF04).
 *
 * Chunk A: apenas o stub de inicialização. Conexão Wi-Fi, cliente MQTT sobre TLS e
 * publicação de telemetria/alerta/heartbeat são implementados no Chunk D.
 */
#ifndef NET_H
#define NET_H

/* Inicializa a pilha de rede (Wi-Fi + MQTT/TLS). No-op no Chunk A. */
void net_init(void);

#endif /* NET_H */
