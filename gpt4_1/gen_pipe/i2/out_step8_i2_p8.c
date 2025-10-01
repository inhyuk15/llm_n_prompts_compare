#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

/* Error code definitions */
typedef enum {
  ERR_OK = 0,                /**< Success */
  ERR_INVALID_PARAM,         /**< Invalid parameter */
  ERR_MUTEX_CREATION_FAILED, /**< Mutex creation failed */
  ERR_MUTEX_TAKE_FAILED,     /**< Mutex take failed */
  ERR_MUTEX_GIVE_FAILED,     /**< Mutex give failed */
  ERR_GPIO_INIT_FAILED,      /**< GPIO initialization failed */
  ERR_INPUT_READ_FAILED,     /**< Input read failed */
  ERR_INPUT_PARSE_FAILED,    /**< Input parse failed */
  ERR_INPUT_RANGE,           /**< Input range error */
  ERR_FATAL = 0xFF           /**< Fatal error */
} error_t;

#define RED_PIN                15U    /**< Stop signal LED GPIO (e.g., 15) */
#define BLUE_PIN               16U    /**< Walk signal LED GPIO (e.g., 16) */
#define GREEN_PIN              17U    /**< Walk blinking signal LED GPIO (e.g., 17) */
#define BLINK_FREQUENCY        2U     /**< Blink frequency per second */
#define BLINK_ON_MS            250U   /**< Blink on duration (ms) */
#define BLINK_OFF_MS           250U   /**< Blink off duration (ms) */
#define MAX_BLINK_CYCLES       10000U /**< Maximum blink cycles */
#define INPUT_BUF_SIZE         16U    /**< Input buffer size */
#define MAX_INPUT_LENGTH       (INPUT_BUF_SIZE - 1U) /**< Maximum input length */

/* Semaphore handle for thread-safe GPIO access */
static SemaphoreHandle_t gpio_mutex = NULL;

/**
 * @brief Initialize GPIOs and create mutex.
 *
 * Initializes GPIO pins as outputs, sets initial level to 0.
 * Creates mutex for thread-safe GPIO access.
 *
 * @return error_t ERR_OK on success, error code otherwise.
 */
static error_t SetupGpio(void) {
  if (gpio_mutex == NULL) {
    gpio_mutex = xSemaphoreCreateMutex();
    if (gpio_mutex == NULL) {
      return ERR_MUTEX_CREATION_FAILED;
    }
  }

  gpio_reset_pin(RED_PIN);
  gpio_reset_pin(BLUE_PIN);
  gpio_reset_pin(GREEN_PIN);

  if (gpio_set_direction(RED_PIN, GPIO_MODE_OUTPUT) != ESP_OK) {
    return ERR_GPIO_INIT_FAILED;
  }
  if (gpio_set_direction(BLUE_PIN, GPIO_MODE_OUTPUT) != ESP_OK) {
    return ERR_GPIO_INIT_FAILED;
  }
  if (gpio_set_direction(GREEN_PIN, GPIO_MODE_OUTPUT) != ESP_OK) {
    return ERR_GPIO_INIT_FAILED;
  }

  if (gpio_set_level(RED_PIN, 0U) != ESP_OK) {
    return ERR_GPIO_INIT_FAILED;
  }
  if (gpio_set_level(BLUE_PIN, 0U) != ESP_OK) {
    return ERR_GPIO_INIT_FAILED;
  }
  if (gpio_set_level(GREEN_PIN, 0U) != ESP_OK) {
    return ERR_GPIO_INIT_FAILED;
  }

  return ERR_OK;
}

/**
 * @brief Turn off all LEDs safely.
 *
 * Takes mutex, sets all LED GPIOs to low level.
 *
 * @return error_t ERR_OK on success, error code otherwise.
 */
static error_t TurnOffAll(void) {
  if (gpio_mutex == NULL) {
    return ERR_MUTEX_TAKE_FAILED;
  }
  if (xSemaphoreTake(gpio_mutex, portMAX_DELAY) != pdTRUE) {
    return ERR_MUTEX_TAKE_FAILED;
  }

  if (gpio_set_level(RED_PIN, 0U) != ESP_OK) {
    (void)xSemaphoreGive(gpio_mutex);
    return ERR_GPIO_INIT_FAILED;
  }
  if (gpio_set_level(BLUE_PIN, 0U) != ESP_OK) {
    (void)xSemaphoreGive(gpio_mutex);
    return ERR_GPIO_INIT_FAILED;
  }
  if (gpio_set_level(GREEN_PIN, 0U) != ESP_OK) {
    (void)xSemaphoreGive(gpio_mutex);
    return ERR_GPIO_INIT_FAILED;
  }

  if (xSemaphoreGive(gpio_mutex) != pdTRUE) {
    return ERR_MUTEX_GIVE_FAILED;
  }

  return ERR_OK;
}

/**
 * @brief Set GPIO level safely.
 *
 * Takes mutex and sets the specified GPIO pin level.
 *
 * @param pin GPIO number.
 * @param level Output level (0 or 1).
 *
 * @return error_t ERR_OK on success, error code otherwise.
 */
static error_t SetGpioLevel(uint32_t pin, uint32_t level) {
  if (gpio_mutex == NULL) {
    return ERR_MUTEX_TAKE_FAILED;
  }
  if (xSemaphoreTake(gpio_mutex, portMAX_DELAY) != pdTRUE) {
    return ERR_MUTEX_TAKE_FAILED;
  }

  if (gpio_set_level(pin, level) != ESP_OK) {
    (void)xSemaphoreGive(gpio_mutex);
    return ERR_GPIO_INIT_FAILED;
  }

  if (xSemaphoreGive(gpio_mutex) != pdTRUE) {
    return ERR_MUTEX_GIVE_FAILED;
  }

  return ERR_OK;
}

/**
 * @brief Delay function in milliseconds.
 *
 * Uses FreeRTOS task delay function with tick calculation.
 *
 * @param ms Delay duration in milliseconds.
 */
static void DelayMs(uint32_t ms) {
  const uint32_t max_ticks = 0xFFFFFFFFU / portTICK_PERIOD_MS;
  uint32_t delay_ticks;

  if (ms > max_ticks) {
    delay_ticks = max_ticks;
  } else {
    delay_ticks = ms / portTICK_PERIOD_MS;
    if ((ms % portTICK_PERIOD_MS) != 0U) {
      delay_ticks++;
    }
  }

  vTaskDelay(delay_ticks);
}

/**
 * @brief Run pedestrian blinking on GREEN_PIN.
 *
 * Blink green LED safely using SetGpioLevel with on/off durations.
 *
 * @param blink_duration_ms Blink duration in milliseconds.
 *
 * @return error_t ERR_OK on success, error code otherwise.
 */
static error_t PedestrianBlink(uint32_t blink_duration_ms) {
  if (BLINK_ON_MS == 0U || BLINK_OFF_MS == 0U) {
    return ERR_INVALID_PARAM;
  }
  if (blink_duration_ms == 0U) {
    return ERR_OK;
  }

  uint32_t cycle_time = BLINK_ON_MS + BLINK_OFF_MS;
  if (cycle_time == 0U) {
    return ERR_INVALID_PARAM;
  }

  uint32_t cycles = blink_duration_ms / cycle_time;
  if (cycles > MAX_BLINK_CYCLES) {
    cycles = MAX_BLINK_CYCLES;
  }

  for (uint32_t i = 0U; i < cycles; i++) {
    error_t err = SetGpioLevel(GREEN_PIN, 1U);
    if (err != ERR_OK) {
      return err;
    }
    vTaskDelay(pdMS_TO_TICKS(BLINK_ON_MS));

    err = SetGpioLevel(GREEN_PIN, 0U);
    if (err != ERR_OK) {
      return err;
    }
    vTaskDelay(pdMS_TO_TICKS(BLINK_OFF_MS));
  }

  return ERR_OK;
}

/**
 * @brief Read positive float input from user.
 *
 * Prompts user, reads a line, parses a float, and checks for positive value.
 *
 * @param prompt Prompt string to output.
 * @param[out] out_value Parsed positive float.
 *
 * @return error_t ERR_OK on success, error code otherwise.
 */
static error_t ReadPositiveFloat(const char *prompt, float *out_value) {
  char input_buf[INPUT_BUF_SIZE];
  int sscanf_result;

  if (prompt == NULL || out_value == NULL) {
    return ERR_INVALID_PARAM;
  }

  if (printf("%s", prompt) < 0) {
    return ERR_INPUT_READ_FAILED;
  }

  if (fgets(input_buf, (int)INPUT_BUF_SIZE, stdin) == NULL) {
    (void)printf("입력 오류\n");
    return ERR_INPUT_READ_FAILED;
  }

  size_t len = strnlen(input_buf, INPUT_BUF_SIZE);
  if (len > 0 && input_buf[len - 1] == '\n') {
    input_buf[len - 1] = '\0';
  }

  sscanf_result = sscanf(input_buf, "%f", out_value);
  if (sscanf_result != 1 || *out_value <= 0.0F) {
    (void)printf("잘못된 입력입니다.\n");
    return ERR_INPUT_PARSE_FAILED;
  }

  return ERR_OK;
}

/**
 * @brief Run traffic light cycle indefinitely.
 *
 * Controls stop and walk signals based on supplied milliseconds.
 *
 * @param stop_time_ms Stop signal duration in ms.
 * @param walk_time_ms Walk signal duration in ms.
 *
 * @return error_t ERR_OK normally never returns; error code otherwise.
 */
static error_t RunTrafficLight(uint32_t stop_time_ms, uint32_t walk_time_ms) {
  if (stop_time_ms == 0U || walk_time_ms == 0U) {
    return ERR_INVALID_PARAM;
  }

  for (;;) {
    error_t err = TurnOffAll();
    if (err != ERR_OK) {
      return err;
    }

    err = SetGpioLevel(RED_PIN, 1U);
    if (err != ERR_OK) {
      return err;
    }

    DelayMs(stop_time_ms);

    err = TurnOffAll();
    if (err != ERR_OK) {
      return err;
    }

    uint32_t walk_time_10th = walk_time_ms / 10U;
    if (walk_time_10th > walk_time_ms) {
      walk_time_10th = 0U;
    }
    uint32_t solid_walk_time_ms = (walk_time_ms > walk_time_10th) ? (walk_time_ms - walk_time_10th) : 0U;
    uint32_t blinking_walk_time_ms = walk_time_10th;

    err = SetGpioLevel(BLUE_PIN, 1U);
    if (err != ERR_OK) {
      return err;
    }
    DelayMs(solid_walk_time_ms);

    err = SetGpioLevel(BLUE_PIN, 0U);
    if (err != ERR_OK) {
      return err;
    }

    err = PedestrianBlink(blinking_walk_time_ms);
    if (err != ERR_OK) {
      return err;
    }
  }
}

/**
 * @brief Application main entry point.
 *
 * Initializes GPIO, reads user input for stop and walk times,
 * and runs traffic light cycle.
 */
void app_main(void) {
  error_t err;
  float stop_time_f = 0.0F;
  float walk_time_f = 0.0F;
  uint32_t stop_time_ms = 0U;
  uint32_t walk_time_ms = 0U;

  err = SetupGpio();
  if (err != ERR_OK) {
    (void)printf("GPIO 초기화 실패: %d\n", (int)err);
    return;
  }

  err = ReadPositiveFloat("정지 신호 시간 (초): ", &stop_time_f);
  if (err != ERR_OK) {
    (void)printf("정지 신호 시간 입력 실패\n");
    return;
  }

  err = ReadPositiveFloat("보행 신호 시간 (초): ", &walk_time_f);
  if (err != ERR_OK) {
    (void)printf("보행 신호 시간 입력 실패\n");
    return;
  }

  double stop_time_ms_double = (double)stop_time_f * 1000.0;
  double walk_time_ms_double = (double)walk_time_f * 1000.0;

  if (stop_time_ms_double <= 0.0 || stop_time_ms_double > (double)UINT32_MAX ||
      walk_time_ms_double <= 0.0 || walk_time_ms_double > (double)UINT32_MAX) {
    (void)printf("시간 값이 범위를 벗어났습니다.\n");
    return;
  }

  stop_time_ms = (uint32_t)stop_time_ms_double;
  walk_time_ms = (uint32_t)walk_time_ms_double;

  if (stop_time_ms == 0U || walk_time_ms == 0U) {
    (void)printf("입력된 시간이 0 또는 비정상적입니다.\n");
    return;
  }

  err = RunTrafficLight(stop_time_ms, walk_time_ms);
  if (err != ERR_OK) {
    (void)printf("신호등 실행 중 오류 발생: %d\n", (int)err);
  }
}