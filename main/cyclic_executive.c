#include <stdio.h>
#include "driver/touch_pad.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <rom/ets_sys.h>
#include "esp_timer.h"

#define TOUCH_THRESH_NO_USE (0)

static void gas_pedal_sensor(int64_t *total_time)
{
  uint16_t gas_pedal_sensor_value;
  touch_pad_read(0, &gas_pedal_sensor_value);

  int64_t start_time = esp_timer_get_time();
  if (gas_pedal_sensor_value < (uint16_t)500)
  {
    ets_delay_us(10); // Simula tempo de propagação pelo fio (computador -> nodo)
    ets_delay_us(5);  // Simula tempo da ação de controle de injeção eletrônica

    int64_t end_time = esp_timer_get_time();
    int64_t elapsed_time = end_time - start_time;
    *total_time += elapsed_time;
    printf("\n[Motor] Injeção Eletrônica Acionada (%lld us)", *total_time);
  }
}

static void engine_temp_sensor(int64_t *total_time)
{
  uint16_t engine_temp_sensor_value;
  touch_pad_read(3, &engine_temp_sensor_value);

  int64_t start_time = esp_timer_get_time();
  if (engine_temp_sensor_value < (uint16_t)500)
  {
    ets_delay_us(10); // Simula tempo de propagação pelo fio (computador -> nodo)
    ets_delay_us(5);  // Simula tempo da ação de controle de injeção eletrônica

    int64_t end_time = esp_timer_get_time();
    int64_t elapsed_time = end_time - start_time;
    *total_time += elapsed_time;
    printf("\n[Motor] Temperatura Elevada (%lld us)", *total_time);
  }
}

static void brake_pedal_sensor(int64_t *total_time)
{
  uint16_t brake_pedal_sensor_value;
  touch_pad_read(4, &brake_pedal_sensor_value);

  int64_t start_time = esp_timer_get_time();
  if (brake_pedal_sensor_value < (uint16_t)500)
  {
    ets_delay_us(10); // Simula tempo de propagação pelo fio (computador -> nodo)
    ets_delay_us(5);  // Simula tempo da ação de controle de injeção eletrônica

    int64_t end_time = esp_timer_get_time();
    int64_t elapsed_time = end_time - start_time;
    *total_time += elapsed_time;
    printf("\n[Frenagem] ABS Acionado (%lld us)", *total_time);
  }
}

static void collision_sensor(int64_t *total_time)
{
  uint16_t collision_sensor_value;
  touch_pad_read(7, &collision_sensor_value);

  int64_t start_time = esp_timer_get_time();
  if (collision_sensor_value < (uint16_t)500)
  {
    ets_delay_us(10); // Simula tempo de propagação pelo fio (computador -> nodo)
    ets_delay_us(5);  // Simula tempo da ação de controle de injeção eletrônica

    int64_t end_time = esp_timer_get_time();
    int64_t elapsed_time = end_time - start_time;
    *total_time += elapsed_time;
    printf("\n[Suporte a Vida] Airbags Acionados (%lld us)", *total_time);
  }
}

static void car_movement_sensor(int64_t *total_time)
{
  uint16_t car_movement_sensor_value;
  touch_pad_read(9, &car_movement_sensor_value);

  int64_t start_time = esp_timer_get_time();
  if (car_movement_sensor_value < (uint16_t)500)
  {
    ets_delay_us(10); // Simula tempo de propagação pelo fio (computador -> nodo)
    ets_delay_us(5);  // Simula tempo da ação de controle de injeção eletrônica

    int64_t end_time = esp_timer_get_time();
    int64_t elapsed_time = end_time - start_time;
    *total_time += elapsed_time;
    printf("\n[Suporte a Vida] Alerta do Cinto de Segurança Acionado (%lld us)", *total_time);
  }
}

void app_main(void)
{
  // Inicializar touch pads
  touch_pad_init();
  touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);

  // Configuração dos sensores touch
  touch_pad_config(0, TOUCH_THRESH_NO_USE); // gas_pedal_sensor
  touch_pad_config(3, TOUCH_THRESH_NO_USE); // engine_temp_sensor
  touch_pad_config(4, TOUCH_THRESH_NO_USE); // brake_pedal_sensor
  touch_pad_config(7, TOUCH_THRESH_NO_USE); // collision_sensor
  touch_pad_config(9, TOUCH_THRESH_NO_USE); // car_movement_sensor

  while (1)
  {
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Atualização do display (1 segundo)

    int64_t total_time = 0;
    total_time += 11;              // 1us (tempo de aquisição da amostra) + 10us (tempo de propagação pelo fio (nodo -> computador))
    gas_pedal_sensor(&total_time); // Deadline de 500 us
    total_time += 11;
    brake_pedal_sensor(&total_time); // Deadline de 100 ms
    total_time += 11;
    collision_sensor(&total_time); // Deadline de 100 ms
    total_time += 11;
    engine_temp_sensor(&total_time); // Deadline de 20 ms
    total_time += 11;
    car_movement_sensor(&total_time); // Deadline de 1 segundo
  }
}