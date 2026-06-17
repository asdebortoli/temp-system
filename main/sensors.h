/*
 * sensors.h — Aquisição de sensores (DS18B20, DHT22, LDR, rede elétrica, pânico, buzzer).
 *
 * Chunk A: apenas o stub de inicialização. Drivers reais e a tarefa de aquisição
 * (fila de sensor_reading_t) são implementados no Chunk B.
 */
#ifndef SENSORS_H
#define SENSORS_H

/* Inicializa o subsistema de sensores. No-op no Chunk A. */
void sensors_init(void);

#endif /* SENSORS_H */
