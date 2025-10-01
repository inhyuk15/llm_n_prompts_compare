#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

/* 에러 코드 체계 정의 */
typedef enum {
    ERR_OK = 0,
    ERR_INVALID_PARAM,
    ERR_MUTEX_CREATION_FAILED,
    ERR_MUTEX_TAKE_FAILED,
    ERR_MUTEX_GIVE_FAILED,
    ERR_GPIO_INIT_FAILED,
    ERR_INPUT_READ_FAILED,
    ERR_INPUT_PARSE_FAILED,
    ERR_INPUT_RANGE,
    ERR_FATAL = 0xFF
} error_t;

#define RED_PIN          15U   /* 정지 신호 LED GPIO (예: 15번) */
#define BLUE_PIN         16U   /* 보행 신호 LED GPIO (예: 16번) */
#define GREEN_PIN        17U   /* 보행 점멸 신호 LED GPIO (예: 17번) */
#define BLINK_FREQUENCY  2U    /* 초당 깜빡임 횟수 2번 */
#define BLINK_ON_MS      250U  /* 점멸 켜짐 시간 (밀리초) */
#define BLINK_OFF_MS     250U  /* 점멸 꺼짐 시간 (밀리초) */
#define MAX_BLINK_CYCLES 10000U /* 최대 점멸 횟수 제한 */
#define INPUT_BUF_SIZE   16U
#define MAX_INPUT_LENGTH (INPUT_BUF_SIZE - 1U)

/* 재진입 가능 및 멀티태스킹 안전 보장을 위해 공유 자원 제어용 세마포어 선언 */
static SemaphoreHandle_t gpio_mutex = NULL;

/* GPIO 설정 */
static error_t setup_gpio(void)
{
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

    /* 초기 상태 모두 끄기 */
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

/* 모든 LED 끄기 (멀티태스킹 안전하게 처리) */
static error_t turn_off_all(void)
{
    if (gpio_mutex == NULL) {
        return ERR_MUTEX_TAKE_FAILED; /* 치명적 에러 */
    }
    if (xSemaphoreTake(gpio_mutex, portMAX_DELAY) != pdTRUE) {
        return ERR_MUTEX_TAKE_FAILED; /* 치명적 에러 */
    }

    if (gpio_set_level(RED_PIN, 0U) != ESP_OK) {
        xSemaphoreGive(gpio_mutex);
        return ERR_GPIO_INIT_FAILED;
    }
    if (gpio_set_level(BLUE_PIN, 0U) != ESP_OK) {
        xSemaphoreGive(gpio_mutex);
        return ERR_GPIO_INIT_FAILED;
    }
    if (gpio_set_level(GREEN_PIN, 0U) != ESP_OK) {
        xSemaphoreGive(gpio_mutex);
        return ERR_GPIO_INIT_FAILED;
    }

    if (xSemaphoreGive(gpio_mutex) != pdTRUE) {
        return ERR_MUTEX_GIVE_FAILED;
    }

    return ERR_OK;
}

/* GPIO 레벨 설정 (멀티태스킹 안전) */
static error_t set_gpio_level(uint32_t pin, uint32_t level)
{
    if (gpio_mutex == NULL) {
        return ERR_MUTEX_TAKE_FAILED; /* 치명적 에러 */
    }
    if (xSemaphoreTake(gpio_mutex, portMAX_DELAY) != pdTRUE) {
        return ERR_MUTEX_TAKE_FAILED; /* 치명적 에러 */
    }

    esp_err_t esp_ret = gpio_set_level(pin, level);
    if (esp_ret != ESP_OK) {
        xSemaphoreGive(gpio_mutex);
        return ERR_GPIO_INIT_FAILED;
    }

    if (xSemaphoreGive(gpio_mutex) != pdTRUE) {
        return ERR_MUTEX_GIVE_FAILED;
    }

    return ERR_OK;
}

/* 밀리초 단위 지연 */
static void delay_ms(uint32_t ms)
{
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

/* 횟수 제한된 보행 점멸 (멀티태스킹 안전하게 처리) */
static error_t pedestrian_blink(uint32_t blink_duration_ms)
{
    if (BLINK_ON_MS == 0U || BLINK_OFF_MS == 0U) {
        return ERR_INVALID_PARAM; /* 치명적 에러 */
    }

    if (blink_duration_ms == 0U) {
        return ERR_OK; /* 복구 가능한 에러 아님, 그냥 무시 */
    }

    uint32_t cycle_time = BLINK_ON_MS + BLINK_OFF_MS;
    if (cycle_time == 0U) {
        return ERR_INVALID_PARAM; /* 치명적 에러 */
    }

    uint32_t cycles = blink_duration_ms / cycle_time;
    if (cycles > MAX_BLINK_CYCLES) {
        cycles = MAX_BLINK_CYCLES;
    }

    for (uint32_t i = 0U; i < cycles; i++) {
        error_t err = set_gpio_level(GREEN_PIN, 1U);
        if (err != ERR_OK) {
            /* 치명적 에러 */
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(BLINK_ON_MS));

        err = set_gpio_level(GREEN_PIN, 0U);
        if (err != ERR_OK) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(BLINK_OFF_MS));
    }

    return ERR_OK;
}

/* 긍정 부동소수점 숫자 입력 받기 */
static error_t read_positive_float(const char *prompt, float *out_value)
{
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

/* 신호등 사이클 실행 (멀티태스킹 중 안전하게 GPIO 제어) */
static error_t run_traffic_light(uint32_t stop_time_ms, uint32_t walk_time_ms)
{
    if (stop_time_ms == 0U || walk_time_ms == 0U) {
        return ERR_INVALID_PARAM;
    }

    for (;;) {
        error_t err = turn_off_all();
        if (err != ERR_OK) {
            return err; /* 치명적 에러 */
        }

        err = set_gpio_level(RED_PIN, 1U);
        if (err != ERR_OK) {
            return err;
        }

        delay_ms(stop_time_ms);

        err = turn_off_all();
        if (err != ERR_OK) {
            return err;
        }

        uint32_t walk_time_10th = walk_time_ms / 10U;
        /* 10분의 1 만큼 점멸, 나머지 고정 */
        if (walk_time_10th > walk_time_ms) {
            walk_time_10th = 0U; /* fail safe */
        }
        uint32_t solid_walk_time_ms = (walk_time_ms > walk_time_10th) ? (walk_time_ms - walk_time_10th) : 0U;
        uint32_t blinking_walk_time_ms = walk_time_10th;

        err = set_gpio_level(BLUE_PIN, 1U);
        if (err != ERR_OK) {
            return err;
        }
        delay_ms(solid_walk_time_ms);

        err = set_gpio_level(BLUE_PIN, 0U);
        if (err != ERR_OK) {
            return err;
        }

        err = pedestrian_blink(blinking_walk_time_ms);
        if (err != ERR_OK) {
            return err;
        }
    }
}

/* app_main 개선: 에러 전파 및 기본값 처리 */
void app_main(void)
{
    error_t err;
    float stop_time_f = 0.0F;
    float walk_time_f = 0.0F;
    uint32_t stop_time_ms = 0U;
    uint32_t walk_time_ms = 0U;

    err = setup_gpio();
    if (err != ERR_OK) {
        (void)printf("GPIO 초기화 실패: %d\n", err);
        return;
    }

    err = read_positive_float("정지 신호 시간 (초): ", &stop_time_f);
    if (err != ERR_OK) {
        (void)printf("정지 신호 시간 입력 실패\n");
        return;
    }

    err = read_positive_float("보행 신호 시간 (초): ", &walk_time_f);
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

    err = run_traffic_light(stop_time_ms, walk_time_ms);
    if (err != ERR_OK) {
        (void)printf("신호등 실행 중 오류 발생: %d\n", err);
    }
}