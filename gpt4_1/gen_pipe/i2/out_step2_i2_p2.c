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
/*
 * Stack usage: ~64 bytes (local variables + small function call overhead)
 */
static void turn_off_all(void);
/*
 * Stack usage: ~32 bytes (small function call overhead)
 */
static void pedestrian_blink(uint32_t blink_duration_ms);
/*
 * Stack usage: ~48 bytes (int i, cycles variables)
 */
static void delay_ms(uint32_t ms);
/*
 * Stack usage: ~32 bytes (calls vTaskDelay)
 */

static void setup_gpio(void)
{
    gpio_reset_pin(RED_PIN);
    gpio_reset_pin(BLUE_PIN);
    gpio_reset_pin(GREEN_PIN);

    gpio_set_direction(RED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(BLUE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(GREEN_PIN, GPIO_MODE_OUTPUT);
}

static void turn_off_all(void)
{
    gpio_set_level(RED_PIN, 0U);
    gpio_set_level(BLUE_PIN, 0U);
    gpio_set_level(GREEN_PIN, 0U);
}

static void delay_ms(uint32_t ms)
{
    const uint32_t max_ticks = 0xFFFFFFFFU / portTICK_PERIOD_MS;
    uint32_t delay_ticks = 0U;

    if (ms > max_ticks) {
        delay_ticks = max_ticks;
    } else {
        delay_ticks = ms / portTICK_PERIOD_MS;
        if ((ms % portTICK_PERIOD_MS) != 0U) {
            delay_ticks = delay_ticks + 1U;
        }
    }

    vTaskDelay(delay_ticks);
}

static void pedestrian_blink(uint32_t blink_duration_ms)
{
    uint32_t cycles = 0U;
    uint32_t i = 0U;

    /* blink_duration_ms가 0이면 동작하지 않음 */
    if (blink_duration_ms == 0U) {
        return;
    }

    /* 초당 깜빡임 횟수에 따른 사이클 계산 */
    /* 1 사이클 = 점멸 켜짐 시간 + 점멸 꺼짐 시간 */
    /* cycles = blink_duration (ms) / (BLINK_ON_MS + BLINK_OFF_MS) */
    cycles = blink_duration_ms / (BLINK_ON_MS + BLINK_OFF_MS);

    if (cycles > MAX_BLINK_CYCLES) {
        cycles = MAX_BLINK_CYCLES;
    }

    for (i = 0U; i < cycles; i++) {
        gpio_set_level(GREEN_PIN, 1U);
        vTaskDelay(pdMS_TO_TICKS(BLINK_ON_MS));
        gpio_set_level(GREEN_PIN, 0U);
        vTaskDelay(pdMS_TO_TICKS(BLINK_OFF_MS));
    }
}

void app_main(void)
{
    int32_t return_code = 0;
    uint32_t solid_walk_time_ms = 0U;
    uint32_t blinking_walk_time_ms = 0U;
    uint32_t stop_time_ms = 0U;
    uint32_t walk_time_ms = 0U;
    float stop_time_f = 0.0F;
    float walk_time_f = 0.0F;
    char input_buf[INPUT_BUF_SIZE];
    int32_t sscanf_result = 0;

    setup_gpio();

    /* 입력 및 검증: 정지 신호 시간 */
    return_code = printf("정지 신호 시간 (초): ");
    (void)return_code;

    if (fgets(input_buf, (int)INPUT_BUF_SIZE, stdin) == NULL) {
        (void)printf("입력 오류\n");
        /* 단일 exit point 규칙 준수 위해 goto 사용 */
        goto exit;
    }

    sscanf_result = sscanf(input_buf, "%f", &stop_time_f);
    if (sscanf_result != 1) {
        (void)printf("잘못된 입력입니다.\n");
        goto exit;
    }

    /* 입력 및 검증: 보행 신호 시간 */
    return_code = printf("보행 신호 시간 (초): ");
    (void)return_code;

    if (fgets(input_buf, (int)INPUT_BUF_SIZE, stdin) == NULL) {
        (void)printf("입력 오류\n");
        goto exit;
    }

    sscanf_result = sscanf(input_buf, "%f", &walk_time_f);
    if (sscanf_result != 1) {
        (void)printf("잘못된 입력입니다.\n");
        goto exit;
    }

    if ((stop_time_f <= 0.0F) || (walk_time_f <= 0.0F)) {
        (void)printf("시간은 0보다 커야 합니다.\n");
        goto exit;
    }

    /* 밀리초 단위로 변환 (부동소수점 곱셈 후 uint32_t로 안전 변환) */
    stop_time_ms = (uint32_t)(stop_time_f * 1000.0F);
    walk_time_ms = (uint32_t)(walk_time_f * 1000.0F);

    for ( ; ; )
    {
        /* 정지 신호 RED ON */
        turn_off_all();
        gpio_set_level(RED_PIN, 1U);
        delay_ms(stop_time_ms);

        /* 보행 신호 BLUE ON */
        turn_off_all();
        /* 10% 미만 구간 연산: FIXED 모델 사용 */
        solid_walk_time_ms = (walk_time_ms * 9U) / 10U; /* 90% */
        blinking_walk_time_ms = walk_time_ms - solid_walk_time_ms;

        gpio_set_level(BLUE_PIN, 1U);
        delay_ms(solid_walk_time_ms);

        /* 보행 점멸 신호 GREEN 깜빡임 (10% 구간) */
        gpio_set_level(BLUE_PIN, 0U);
        pedestrian_blink(blinking_walk_time_ms);
    }

exit:
    /* 단일 exit point */
    return;
}