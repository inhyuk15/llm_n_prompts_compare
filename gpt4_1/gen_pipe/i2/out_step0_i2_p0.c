#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define RED_PIN   15  // 정지 신호 LED GPIO (예: 15번)
#define BLUE_PIN  16  // 보행 신호 LED GPIO (예: 16번)
#define GREEN_PIN 17  // 보행 점멸 신호 LED GPIO (예: 17번)

void setup_gpio() {
    gpio_reset_pin(RED_PIN);
    gpio_reset_pin(BLUE_PIN);
    gpio_reset_pin(GREEN_PIN);
    
    gpio_set_direction(RED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(BLUE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(GREEN_PIN, GPIO_MODE_OUTPUT);
}

void turn_off_all() {
    gpio_set_level(RED_PIN, 0);
    gpio_set_level(BLUE_PIN, 0);
    gpio_set_level(GREEN_PIN, 0);
}

void delay_seconds(float sec) {
    vTaskDelay((int)(sec * 1000 / portTICK_PERIOD_MS));
}

void pedestrian_blink(float blink_duration) {
    // 보행 점멸 신호: 초당 2번 깜빡임 (0.25초 on, 0.25초 off)
    int cycles = (int)(blink_duration * 2);
    for (int i = 0; i < cycles; i++) {
        gpio_set_level(GREEN_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(250));
        gpio_set_level(GREEN_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void app_main(void) {
    setup_gpio();

    float stop_time = 0;
    float walk_time = 0;

    printf("정지 신호 시간 (초): ");
    scanf("%f", &stop_time);
    printf("보행 신호 시간 (초): ");
    scanf("%f", &walk_time);

    if (stop_time <= 0 || walk_time <= 0) {
        printf("시간은 0보다 커야 합니다.\n");
        return;
    }

    while (1) {
        // 정지 신호 RED ON
        turn_off_all();
        gpio_set_level(RED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS((int)(stop_time * 1000)));

        // 보행 신호 BLUE ON (10% 초과 구간 전까지)
        turn_off_all();
        float solid_walk_time = walk_time * 0.9f;
        float blinking_walk_time = walk_time - solid_walk_time;

        gpio_set_level(BLUE_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS((int)(solid_walk_time * 1000)));

        // 보행 점멸 신호 GREEN 깜빡임 (10% 구간)
        gpio_set_level(BLUE_PIN, 0);
        pedestrian_blink(blinking_walk_time);
    }
}