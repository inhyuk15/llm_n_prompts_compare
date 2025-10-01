#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

/* 에러 코드 체계 정의 */
typedef enum {
    ERR_OK = 0,                /**< 정상 처리 */
    ERR_INVALID_PARAM,         /**< 잘못된 파라미터 */
    ERR_MUTEX_CREATION_FAILED, /**< 뮤텍스 생성 실패 */
    ERR_MUTEX_TAKE_FAILED,     /**< 뮤텍스 획득 실패 */
    ERR_MUTEX_GIVE_FAILED,     /**< 뮤텍스 반환 실패 */
    ERR_GPIO_INIT_FAILED,      /**< GPIO 초기화 실패 */
    ERR_INPUT_READ_FAILED,     /**< 입력 읽기 실패 */
    ERR_INPUT_PARSE_FAILED,    /**< 입력 파싱 실패 */
    ERR_INPUT_RANGE,           /**< 입력 범위 오류 */
    ERR_FATAL = 0xFF          /**< 치명적 에러 */
} error_t;

#define RED_PIN          15U    /**< 정지 신호 LED GPIO (예: 15번) */
#define BLUE_PIN         16U    /**< 보행 신호 LED GPIO (예: 16번) */
#define GREEN_PIN        17U    /**< 보행 점멸 신호 LED GPIO (예: 17번) */
#define BLINK_FREQUENCY  2U     /**< 초당 깜빡임 횟수 2번 */
#define BLINK_ON_MS      250U   /**< 점멸 켜짐 시간 (밀리초) */
#define BLINK_OFF_MS     250U   /**< 점멸 꺼짐 시간 (밀리초) */
#define MAX_BLINK_CYCLES 10000U /**< 최대 점멸 횟수 제한 */
#define INPUT_BUF_SIZE   16U    /**< 입력 버퍼 크기 */
#define MAX_INPUT_LENGTH (INPUT_BUF_SIZE - 1U) /**< 입력 최대 길이 */

/* 재진입 가능 및 멀티태스킹 안전 보장을 위해 공유 자원 제어용 세마포어 선언 */
static SemaphoreHandle_t gpio_mutex = NULL;

/**
 * @brief GPIO 초기화 및 세마포어 생성
 *
 * GPIO 핀을 초기화하고, 출력 모드로 설정하며 초기 레벨을 0으로 세팅한다.
 * 세마포어를 생성하여 멀티태스킹 간 GPIO 제어를 안전하게 한다.
 *
 * @retval ERR_OK 성공
 * @retval ERR_MUTEX_CREATION_FAILED 뮤텍스 생성 실패
 * @retval ERR_GPIO_INIT_FAILED GPIO 초기화 실패
 */
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

/**
 * @brief 모든 LED 끄기 (멀티태스킹 안전)
 *
 * 세마포어를 획득하여 GPIO 상태를 변경하며,
 * 모든 LED 핀을 0으로 세팅한다.
 *
 * @retval ERR_OK 성공
 * @retval ERR_MUTEX_TAKE_FAILED 뮤텍스 획득 실패
 * @retval ERR_MUTEX_GIVE_FAILED 뮤텍스 반환 실패
 * @retval ERR_GPIO_INIT_FAILED GPIO 설정 실패
 */
static error_t turn_off_all(void)
{
    if (gpio_mutex == NULL) {
        return ERR_MUTEX_TAKE_FAILED; /* 치명적 에러 */
    }
    if (xSemaphoreTake(gpio_mutex, portMAX_DELAY) != pdTRUE) {
        return ERR_MUTEX_TAKE_FAILED; /* 치명적 에러 */
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
 * @brief 지정한 GPIO 핀의 레벨 설정 (멀티태스킹 안전)
 *
 * 세마포어를 획득하여 안전하게 GPIO 레벨을 설정한다.
 *
 * @param pin GPIO 번호
 * @param level 출력 레벨 (0 또는 1)
 *
 * @retval ERR_OK 성공
 * @retval ERR_MUTEX_TAKE_FAILED 뮤텍스 획득 실패
 * @retval ERR_MUTEX_GIVE_FAILED 뮤텍스 반환 실패
 * @retval ERR_GPIO_INIT_FAILED GPIO 설정 실패
 */
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
        (void)xSemaphoreGive(gpio_mutex);
        return ERR_GPIO_INIT_FAILED;
    }

    if (xSemaphoreGive(gpio_mutex) != pdTRUE) {
        return ERR_MUTEX_GIVE_FAILED;
    }

    return ERR_OK;
}

/**
 * @brief 밀리초 단위 지연 처리
 *
 * FreeRTOS 태스크 지연 함수를 사용하여 지정된 밀리초만큼 지연한다.
 * 최대 지연시간을 portTICK_PERIOD_MS 단위로 처리한다.
 *
 * @param ms 지연할 밀리초 시간
 */
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

/**
 * @brief 보행 점멸 신호 실행 (멀티태스킹 안전)
 *
 * 지정된 시간동안 GREEN_PIN을 켜고 끄며 점멸한다.
 * BLINK_ON_MS 및 BLINK_OFF_MS로 점멸 주기를 설정하며,
 * 최대 점멸 횟수를 제한한다.
 *
 * @param blink_duration_ms 점멸 시간 (밀리초)
 *
 * @retval ERR_OK 성공
 * @retval ERR_INVALID_PARAM 파라미터 오류
 * @retval ERR_MUTEX_TAKE_FAILED 뮤텍스 획득 실패
 * @retval ERR_MUTEX_GIVE_FAILED 뮤텍스 반환 실패
 * @retval ERR_GPIO_INIT_FAILED GPIO 설정 실패
 */
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

/**
 * @brief 사용자로부터 양수 부동소수점 입력 읽기
 *
 * 콘솔에 프롬프트를 출력하고, 사용자 입력을 받아
 * 부동소수점 숫자로 변환하며, 양수를 검사한다.
 *
 * @param[in] prompt 사용자에게 출력할 프롬프트 문자열
 * @param[out] out_value 읽은 부동소수점 값 저장 위치
 *
 * @retval ERR_OK 정상 처리
 * @retval ERR_INVALID_PARAM 파라미터 오류
 * @retval ERR_INPUT_READ_FAILED 입력 읽기 실패
 * @retval ERR_INPUT_PARSE_FAILED 파싱 실패 또는 음수/0 입력
 */
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

/**
 * @brief 신호등 사이클 실행 (멀티태스킹 안전)
 *
 * 주어진 정지 시간과 보행 시간(ms)으로 무한 루프 내에서
 * 신호등을 제어한다.
 * - 정지 신호(적색) 켜기
 * - 보행 신호(청색) 켜기 및 점멸(녹색) 처리
 *
 * @param stop_time_ms 정지 신호 시간 (밀리초)
 * @param walk_time_ms 보행 신호 시간 (밀리초)
 *
 * @retval ERR_OK 정상 처리 (함수 종료는 일반적이지 않음)
 * @retval ERR_INVALID_PARAM 파라미터 오류
 * @retval 기타 GPIO 제어 관련 오류
 */
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

/**
 * @brief 프로그램 진입점
 *
 * GPIO 초기화 후 사용자로부터 정지 신호 시간과 보행 신호 시간을
 * 입력받아 신호등 사이클을 실행한다.
 * 입력 오류 및 실행 중 오류를 처리하여 메시지를 출력한다.
 */
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