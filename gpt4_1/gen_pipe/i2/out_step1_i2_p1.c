#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define RED_PIN   15  // 정지 신호 LED GPIO (예: 15번)
#define BLUE_PIN  16  // 보행 신호 LED GPIO (예: 16번)
#define GREEN_PIN 17  // 보행 점멸 신호 LED GPIO (예: 17번)

static void setup_gpio(void);
/*
 * Stack usage: ~64 bytes (local variables + small function call overhead)
 */
static void turn_off_all(void);
/*
 * Stack usage: ~32 bytes (small function call overhead)
 */
static void pedestrian_blink(float blink_duration);
/*
 * Stack usage: ~48 bytes (int i, cycles variables)
 */
static void delay_ms(uint32_t ms);
/*
 * Stack usage: ~32 bytes (calls vTaskDelay)
 */

static void setup_gpio(void) {
    gpio_reset_pin(RED_PIN);
    gpio_reset_pin(BLUE_PIN);
    gpio_reset_pin(GREEN_PIN);

    gpio_set_direction(RED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(BLUE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(GREEN_PIN, GPIO_MODE_OUTPUT);
}

static void turn_off_all(void) {
    gpio_set_level(RED_PIN, 0);
    gpio_set_level(BLUE_PIN, 0);
    gpio_set_level(GREEN_PIN, 0);
}

static void delay_ms(uint32_t ms) {
    // Delay helper in milliseconds
    // Protect against too large value although unlikely here
    if (ms > 0xFFFFFFFF / portTICK_PERIOD_MS) {
        vTaskDelay(pdMS_TO_TICKS(0xFFFFFFFF / portTICK_PERIOD_MS));
    } else {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

static void pedestrian_blink(float blink_duration) {
    // 보행 점멸 신호: 초당 2번 깜빡임 (0.25초 on, 0.25초 off)
    // Prevent negative or unreasonable input for safety
    if (blink_duration <= 0.0f) return;

    int cycles = (int)(blink_duration * 2);
    if (cycles > 10000) cycles = 10000; // upper cap to avoid too long blinking causes overflow

    for (int i = 0; i < cycles; i++) {
        gpio_set_level(GREEN_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(250));
        gpio_set_level(GREEN_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void app_main(void) {
    setup_gpio();

    // Use static variables for input to avoid large stack usage
    static float stop_time = 0.0f;
    static float walk_time = 0.0f;

    // Buffer for fgets input, static and fixed size
    static char input_buf[16]; // Enough for floats like '1234.56' + newline + null terminator
    
    printf("정지 신호 시간 (초): ");
    if (fgets(input_buf, sizeof(input_buf), stdin) == NULL) {
        printf("입력 오류\n");
        return;
    }
    if (sscanf(input_buf, "%f", &stop_time) != 1) {
        printf("잘못된 입력입니다.\n");
        return;
    }

    printf("보행 신호 시간 (초): ");
    if (fgets(input_buf, sizeof(input_buf), stdin) == NULL) {
        printf("입력 오류\n");
        return;
    }
    if (sscanf(input_buf, "%f", &walk_time) != 1) {
        printf("잘못된 입력입니다.\n");
        return;
    }

    if (stop_time <= 0.0f || walk_time <= 0.0f) {
        printf("시간은 0보다 커야 합니다.\n");
        return;
    }

    while (1) {
        // 정지 신호 RED ON
        turn_off_all();
        gpio_set_level(RED_PIN, 1);
        delay_ms((uint32_t)(stop_time * 1000));

        // 보행 신호 BLUE ON (10% 초과 구간 전까지)
        turn_off_all();
        float solid_walk_time = walk_time * 0.9f;
        float blinking_walk_time = walk_time - solid_walk_time;

        gpio_set_level(BLUE_PIN, 1);
        delay_ms((uint32_t)(solid_walk_time * 1000));

        // 보행 점멸 신호 GREEN 깜빡임 (10% 구간)
        gpio_set_level(BLUE_PIN, 0);
        pedestrian_blink(blinking_walk_time);
    }
}