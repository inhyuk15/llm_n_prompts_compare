#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define RED_PIN          15U   /* 정지 신호 LED GPIO (예: 15번) */
#define BLUE_PIN         16U   /* 보행 신호 LED GPIO (예: 16번) */
#define GREEN_PIN        17U   /* 보행 점멸 신호 LED GPIO (예: 17번) */
#define BLINK_FREQUENCY  2U    /* 초당 깜빡임 횟수 2번 */
#define BLINK_ON_MS      250U  /* 점멸 켜짐 시간 (밀리초) */
#define BLINK_OFF_MS     250U  /* 점멸 꺼짐 시간 (밀리초) */
#define MAX_BLINK_CYCLES 10000U /* 최대 점멸 횟수 제한 */
#define INPUT_BUF_SIZE   16U

static void setup_gpio(void);
static void turn_off_all(void);
static void delay_ms(uint32_t ms);
static void pedestrian_blink(uint32_t blink_duration_ms);
static int read_positive_float(const char *prompt, float *out_value);
static void run_traffic_light(uint32_t stop_time_ms, uint32_t walk_time_ms);

/* GPIO 설정 */
static void setup_gpio(void)
{
    gpio_reset_pin(RED_PIN);
    gpio_reset_pin(BLUE_PIN);
    gpio_reset_pin(GREEN_PIN);

    gpio_set_direction(RED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(BLUE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(GREEN_PIN, GPIO_MODE_OUTPUT);
}

/* 모든 LED 끄기 */
static void turn_off_all(void)
{
    gpio_set_level(RED_PIN, 0U);
    gpio_set_level(BLUE_PIN, 0U);
    gpio_set_level(GREEN_PIN, 0U);
}

/* 밀리초 단위 지연 */
static void delay_ms(uint32_t ms)
{
    const uint32_t max_ticks = 0xFFFFFFFFU / portTICK_PERIOD_MS;
    uint32_t delay_ticks = 0U;

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

/* 횟수 제한된 보행 점멸 */
static void pedestrian_blink(uint32_t blink_duration_ms)
{
    if (blink_duration_ms == 0U) {
        return;
    }

    uint32_t cycle_time = BLINK_ON_MS + BLINK_OFF_MS;
    uint32_t cycles = blink_duration_ms / cycle_time;

    if (cycles > MAX_BLINK_CYCLES) {
        cycles = MAX_BLINK_CYCLES;
    }

    for (uint32_t i = 0U; i < cycles; i++) {
        gpio_set_level(GREEN_PIN, 1U);
        vTaskDelay(pdMS_TO_TICKS(BLINK_ON_MS));
        gpio_set_level(GREEN_PIN, 0U);
        vTaskDelay(pdMS_TO_TICKS(BLINK_OFF_MS));
    }
}

/* 긍정 부동소수점 숫자 입력 받기 */
static int read_positive_float(const char *prompt, float *out_value)
{
    char input_buf[INPUT_BUF_SIZE];
    int sscanf_result = 0;

    if (printf("%s", prompt) < 0) {
        return -1;
    }

    if (fgets(input_buf, (int)INPUT_BUF_SIZE, stdin) == NULL) {
        (void)printf("입력 오류\n");
        return -1;
    }

    sscanf_result = sscanf(input_buf, "%f", out_value);
    if (sscanf_result != 1 || *out_value <= 0.0F) {
        (void)printf("잘못된 입력입니다.\n");
        return -1;
    }

    return 0;
}

/* 신호등 사이클 실행 */
static void run_traffic_light(uint32_t stop_time_ms, uint32_t walk_time_ms)
{
    for ( ; ; ) {
        /* 정지 신호 RED ON */
        turn_off_all();
        gpio_set_level(RED_PIN, 1U);
        delay_ms(stop_time_ms);

        /* 보행 신호 BLUE ON */
        turn_off_all();
        uint32_t solid_walk_time_ms = (walk_time_ms * 9U) / 10U; /* 90% */
        uint32_t blinking_walk_time_ms = walk_time_ms - solid_walk_time_ms;

        gpio_set_level(BLUE_PIN, 1U);
        delay_ms(solid_walk_time_ms);

        /* 보행 점멸 신호 GREEN 깜빡임 (10% 구간) */
        gpio_set_level(BLUE_PIN, 0U);
        pedestrian_blink(blinking_walk_time_ms);
    }
}

void app_main(void)
{
    int ret;
    float stop_time_f = 0.0F;
    float walk_time_f = 0.0F;
    uint32_t stop_time_ms = 0U;
    uint32_t walk_time_ms = 0U;

    setup_gpio();

    ret = read_positive_float("정지 신호 시간 (초): ", &stop_time_f);
    if (ret != 0) {
        return;
    }
    ret = read_positive_float("보행 신호 시간 (초): ", &walk_time_f);
    if (ret != 0) {
        return;
    }

    stop_time_ms = (uint32_t)(stop_time_f * 1000.0F);
    walk_time_ms = (uint32_t)(walk_time_f * 1000.0F);

    run_traffic_light(stop_time_ms, walk_time_ms);
}