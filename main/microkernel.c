#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "driver/touch_pad.h"
#include "soc/rtc_periph.h"
#include "soc/sens_periph.h"

#include "freertos/queue.h"
#include "esp_timer.h"
#include <rom/ets_sys.h>

static const char *TAG = "Touch pad";

#define TOUCH_THRESH_NO_USE (0)
#define TOUCH_THRESH_PERCENT (80)
#define TOUCHPAD_FILTER_TOUCH_PERIOD (10)

static bool s_pad_activated[TOUCH_PAD_MAX];
static uint32_t s_pad_init_val[TOUCH_PAD_MAX];

QueueHandle_t xDisplayQueue;

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

// Helper function to simulate sensor activation and send messages
static void sensor_task(const char *sensor_name, uint16_t touch_pad_num, uint64_t max_deadline_us, TickType_t delay_ticks)
{
  int64_t start_time = esp_timer_get_time();

  touch_pad_intr_enable();

  ets_delay_us(1);  // Simula tempo de aquisição da amostra
  ets_delay_us(10); // Simula tempo de propagação pelo fio (nodo -> computador)

  if (s_pad_activated[touch_pad_num] == true)
  {
    ets_delay_us(10); // Simula tempo de propagação pelo fio (computador -> nodo)
    ets_delay_us(5);  // Simula tempo da ação de controle de injeção eletrônica

    printf("\n%s", sensor_name);

    uint64_t end_time = esp_timer_get_time();
    uint64_t elapsed_time = end_time - start_time;

    if (elapsed_time > max_deadline_us)
    {
      printf(" - Deadline missed (%lld us)", elapsed_time);
    }
    else
    {
      printf(" - Deadline met (%lld us)", elapsed_time);
    }

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%s (%lld us)", sensor_name, elapsed_time);

    // Send the message to the display task
    if (xQueueSend(xDisplayQueue, buffer, portMAX_DELAY) != pdPASS)
    {
      printf("\n[Error] Could not send to display queue.");
    }

    s_pad_activated[touch_pad_num] = false;
  }

  vTaskDelay(delay_ticks);
}

// Specific sensor tasks with appropriate delays and deadlines
static void gas_pedal_sensor(void *pvParameter)
{
  while (1)
  {
    sensor_task("Motor", 0, 500, (1 / portTICK_PERIOD_MS)); // Deadline 500 us, short delay
  }
}

static void engine_temp_sensor(void *pvParameter)
{
  while (1)
  {
    sensor_task("Engine Temp", 3, 20000, (10 / portTICK_PERIOD_MS)); // Deadline 20 ms
  }
}

static void brake_pedal_sensor(void *pvParameter)
{
  while (1)
  {
    sensor_task("ABS", 4, 100000, (50 / portTICK_PERIOD_MS)); // Deadline 100 ms
  }
}

static void collision_sensor(void *pvParameter)
{
  while (1)
  {
    sensor_task("Airbag", 7, 100000, (50 / portTICK_PERIOD_MS)); // Deadline 100 ms
  }
}

static void car_movement_sensor(void *pvParameter)
{
  while (1)
  {
    sensor_task("Seatbelt Alert", 9, 1000000, (500 / portTICK_PERIOD_MS)); // Deadline 1 second
  }
}

static void display(void *pvParameter)
{
  char receivedMessage[64];

  while (1)
  {
    // Check if there is a message in the queue
    if (xQueueReceive(xDisplayQueue, receivedMessage, portMAX_DELAY) == pdPASS)
    {
      // Print the received message
      printf("\n[Display] %s", receivedMessage);
    }

    // Delay for 1 second
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void app_main(void)
{
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
  // Create a queue capable of holding 10 strings of 64 characters
  xDisplayQueue = xQueueCreate(10, sizeof(char) * 64);

  if (xDisplayQueue != NULL)
  {
    // Create the sensor tasks with appropriate priorities
    xTaskCreate(gas_pedal_sensor, "gas_pedal_sensor", 2048, NULL, 3, NULL);
    xTaskCreate(engine_temp_sensor, "engine_temp_sensor", 2048, NULL, 1, NULL);
    xTaskCreate(brake_pedal_sensor, "brake_pedal_sensor", 2048, NULL, 2, NULL);
    xTaskCreate(collision_sensor, "collision_sensor", 2048, NULL, 4, NULL);
    xTaskCreate(car_movement_sensor, "car_movement_sensor", 2048, NULL, 1, NULL);

    // Create the display task
    xTaskCreate(display, "display", 2048, NULL, 1, NULL);
  }
  else
  {
    printf("Failed to create display queue.\n");
  }
}
