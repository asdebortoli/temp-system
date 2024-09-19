#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "driver/touch_pad.h"
#include "soc/rtc_periph.h"
#include "soc/sens_periph.h"
#include "esp_timer.h"
#include <rom/ets_sys.h>

static const char *TAG = "Touch pad";

#define TOUCH_THRESH_NO_USE (0)
#define TOUCH_THRESH_PERCENT (80)
#define TOUCHPAD_FILTER_TOUCH_PERIOD (10)

static bool s_pad_activated[TOUCH_PAD_MAX];
static uint32_t s_pad_init_val[TOUCH_PAD_MAX];

/*
  Read values sensed at all available touch pads.
  Use 2 / 3 of read value as the threshold
  to trigger interrupt when the pad is touched.
  Note: this routine demonstrates a simple way
  to configure activation threshold for the touch pads.
  Do not touch any pads when this routine
  is running (on application start).
 */
static void tp_example_set_thresholds(void)
{
  uint16_t touch_value;
  for (int i = 0; i < TOUCH_PAD_MAX; i++)
  {
    // read filtered value
    touch_pad_read_filtered(i, &touch_value);
    s_pad_init_val[i] = touch_value;
    ESP_LOGI(TAG, "test init: touch pad [%d] val is %d", i, touch_value);
    // set interrupt threshold.
    ESP_ERROR_CHECK(touch_pad_set_thresh(i, touch_value * 2 / 3));
  }
}

/*
  Handle an interrupt triggered when a pad is touched.
  Recognize what pad has been touched and save it in a table.
 */
static void tp_example_rtc_intr(void *arg)
{
  uint32_t pad_intr = touch_pad_get_status();
  // clear interrupt
  touch_pad_clear_status();
  for (int i = 0; i < TOUCH_PAD_MAX; i++)
  {
    if ((pad_intr >> i) & 0x01)
    {
      s_pad_activated[i] = true;
    }
  }
}

/*
 * Before reading touch pad, we need to initialize the RTC IO.
 */
static void tp_example_touch_pad_init(void)
{
  for (int i = 0; i < TOUCH_PAD_MAX; i++)
  {
    // init RTC IO and mode for touch pad.
    touch_pad_config(i, TOUCH_THRESH_NO_USE);
  }
}

static void gas_pedal_sensor(int64_t *total_time)
{
  uint16_t touch_pad_num = 0;
  uint64_t start_time = esp_timer_get_time();

  touch_pad_intr_enable();
  if (s_pad_activated[touch_pad_num] == true)
  {
    ets_delay_us(10); // Simula tempo de propagação pelo fio (computador -> nodo)
    ets_delay_us(5);  // Simula tempo da ação de controle de injeção eletrônica

    uint64_t end_time = esp_timer_get_time();
    uint64_t elapsed_time = end_time - start_time;
    *total_time += elapsed_time;
    printf("\n[Motor] Injeção Eletrônica Acionada (%lld us)", *total_time);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    s_pad_activated[touch_pad_num] = false;
  }
}

static void engine_temp_sensor(int64_t *total_time)
{
  uint16_t touch_pad_num = 3;
  uint64_t start_time = esp_timer_get_time();

  touch_pad_intr_enable();
  if (s_pad_activated[touch_pad_num] == true)
  {
    ets_delay_us(10); // Simula tempo de propagação pelo fio (computador -> nodo)
    ets_delay_us(5);  // Simula tempo da ação de controle de injeção eletrônica

    uint64_t end_time = esp_timer_get_time();
    uint64_t elapsed_time = end_time - start_time;
    *total_time += elapsed_time;
    printf("\n[Motor] Temperatura Elevada (%lld us)", *total_time);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    s_pad_activated[touch_pad_num] = false;
  }
}

static void brake_pedal_sensor(int64_t *total_time)
{
  uint16_t touch_pad_num = 4;
  uint64_t start_time = esp_timer_get_time();

  touch_pad_intr_enable();
  if (s_pad_activated[touch_pad_num] == true)
  {
    ets_delay_us(10); // Simula tempo de propagação pelo fio (computador -> nodo)
    ets_delay_us(5);  // Simula tempo da ação de controle de injeção eletrônica

    uint64_t end_time = esp_timer_get_time();
    uint64_t elapsed_time = end_time - start_time;
    *total_time += elapsed_time;
    printf("\n[Frenagem] ABS Acionado (%lld us)", *total_time);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    s_pad_activated[touch_pad_num] = false;
  }
}

static void collision_sensor(int64_t *total_time)
{
  uint16_t touch_pad_num = 7;
  uint64_t start_time = esp_timer_get_time();

  touch_pad_intr_enable();
  if (s_pad_activated[touch_pad_num] == true)
  {
    ets_delay_us(10); // Simula tempo de propagação pelo fio (computador -> nodo)
    ets_delay_us(5);  // Simula tempo da ação de controle de injeção eletrônica

    uint64_t end_time = esp_timer_get_time();
    uint64_t elapsed_time = end_time - start_time;
    *total_time += elapsed_time;
    printf("\n[Suporte a Vida] Airbags Acionados (%lld us)", *total_time);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    s_pad_activated[touch_pad_num] = false;
  }
}

static void car_movement_sensor(int64_t *total_time)
{
  uint16_t touch_pad_num = 9;
  uint64_t start_time = esp_timer_get_time();

  touch_pad_intr_enable();
  if (s_pad_activated[touch_pad_num] == true)
  {
    ets_delay_us(10); // Simula tempo de propagação pelo fio (computador -> nodo)
    ets_delay_us(5);  // Simula tempo da ação de controle de injeção eletrônica

    uint64_t end_time = esp_timer_get_time();
    uint64_t elapsed_time = end_time - start_time;
    *total_time += elapsed_time;
    printf("\n[Suporte a Vida] Alerta do Cinto de Segurança Acionado (%lld us)", *total_time);

    vTaskDelay(10 / portTICK_PERIOD_MS);

    s_pad_activated[touch_pad_num] = false;
  }
}

void app_main(void)
{
  uint64_t eus, eus2;

  eus = esp_timer_get_time();
  // Initialize touch pad peripheral, it will start a timer to run a filter
  ESP_LOGI(TAG, "Initializing touch pad");
  touch_pad_init();
  // If use interrupt trigger mode, should set touch sensor FSM mode at 'TOUCH_FSM_MODE_TIMER'.
  touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
  // Set reference voltage for charging/discharging
  // For most usage scenarios, we recommend using the following combination:
  // the high reference valtage will be 2.7V - 1V = 1.7V, The low reference voltage will be 0.5V.
  touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
  // Init touch pad IO
  tp_example_touch_pad_init();
  // Initialize and start a software filter to detect slight change of capacitance.
  touch_pad_filter_start(TOUCHPAD_FILTER_TOUCH_PERIOD);
  // Set thresh hold
  tp_example_set_thresholds();
  // Register touch interrupt ISR
  touch_pad_isr_register(tp_example_rtc_intr, NULL);
  // Start a task to show what pads have been touched
  eus2 = esp_timer_get_time();
  printf("\nTempo: %llu\n - Value of Tick in 1 ms: %lu", (eus2 - eus), portTICK_PERIOD_MS);

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
