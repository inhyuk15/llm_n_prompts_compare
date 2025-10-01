#include <stdio.h>
#include <string.h>
#include <math.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// LCD 16x2 관련 헤더 (예제용, 실제 보드 환경에 맞게 변경 필요)
#include "lcd.h"

#define BUTTON_PIN_NUM     0U  /* 버튼 신호 입력 핀 예, 실제 핀번호 보드에 맞게 변경 */
#define BUTTON_PIN_OP      1U  /* 연산자 입력 버튼 핀 */
#define BUTTON_PIN_EQ      2U  /* '=' 버튼 핀 */
#define BUTTON_PIN_CLR     3U  /* 'Clear' 버튼 핀 */

/* LCD 핀, 포트 예제 (보드 핀에 맞게 조정) */
#define LCD_RS  5U
#define LCD_EN  6U
#define LCD_D4  7U
#define LCD_D5  8U
#define LCD_D6  9U
#define LCD_D7  10U

/* Delay times in milliseconds */
#define BUTTON_DEBOUNCE_DELAY_MS 50U
#define BUTTON_HOLD_DELAY_MS 10U
#define BUTTON_REPEAT_DELAY_MS 300U
#define MAIN_LOOP_DELAY_MS 10U

static const char * const TAG = "Calc";

/* 계산 상태 */
typedef enum {
    INPUT_FIRST = 0,
    INPUT_OPERATOR,
    INPUT_SECOND,
    SHOW_RESULT
} CalcState;

typedef enum {
    OP_NONE = 0,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_SIN,
    OP_COS,
    OP_TAN,
    OP_LOG,
    OP_EXP,
    OP_POW
} Operator;

typedef struct {
    char display[17]; /* 16 chars + null-terminator */
    double first;
    double second;
    Operator op;
    CalcState state;
} Calculator;

/* 정적으로 할당된 전역변수로 함수 스코프 제한 위해 static 사용 */
static Calculator calc;

/* 정적 LCD 핸들 */
static LCD_Handle_t lcd;

/**
 * @brief 스택 사용량: lcd_create, lcd_init, lcd_clear 내부에서 사용하는 스택 제외, 
 *                   이 함수 내 로컬변수 없음, 즉 거의 0바이트
 */
static void lcd_init_custom(void)
{
    lcd = lcd_create(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
    lcd_init(lcd, 16U, 2U);
    lcd_clear(lcd);
}

/**
 * @brief 스택 사용량: lcd_set_cursor, lcd_print 내부 스택량 제외,
 *                   함수 내 로컬변수 없음, 0바이트
 *
 * @param str 출력할 문자열, 최대 16바이트 이상 보장 필요
 * @param line 0 또는 1 (LCD 16x2)
 */
static void lcd_display_line(const char* str, uint8_t line)
{
    lcd_set_cursor(lcd, 0U, line);
    lcd_print(lcd, str);
}

/**
 * @brief 스택 사용량: 내부 vTaskDelay 호출 및 간단 비교, 로컬변수 없음 0바이트
 *
 * @param pin gpio_num_t 버튼 핀 번호
 * @return true 버튼 눌림 감지 (디바운스 적용됨)
 * @return false 눌리지 않음
 */
static bool button_pressed(const gpio_num_t pin)
{
    bool pressed;
    pressed = false;

    if (gpio_get_level(pin) == 0) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_DELAY_MS));
        if (gpio_get_level(pin) == 0) {
            while (gpio_get_level(pin) == 0) {
                vTaskDelay(pdMS_TO_TICKS(BUTTON_HOLD_DELAY_MS));
            }
            pressed = true;
        }
    }
    return pressed;
}

/**
 * @brief 문자열을 double로 변환 (atof 내부 사용)
 * 스택 사용량 = atof 내부 사용량 (단순)
 *
 * @param str 숫자 문자열
 * @return double 변환값
 */
static double str_to_double(const char * const str)
{
    double result;
    result = 0.0;
    if (str != NULL) {
        result = atof(str);
    }
    return result;
}

/**
 * @brief 두 개 숫자와 연산자로 결과 계산
 *
 * 스택 사용량: pow 함수 호출 시 약간 증가 (내부 구현에 따름), 
 *             로컬 변수 없음
 *
 * @param a 첫 번째 피연산자
 * @param b 두 번째 피연산자
 * @param op 적용할 연산자
 * @return 계산 결과 (double)
 */
static double calculate(const double a, const double b, const Operator op)
{
    double result;
    result = 0.0;

    switch (op) {
    case OP_ADD:
        result = a + b;
        break;
    case OP_SUB:
        result = a - b;
        break;
    case OP_MUL:
        result = a * b;
        break;
    case OP_DIV:
        if (b != 0.0) {
            result = a / b;
        } else {
            result = 0.0;
        }
        break;
    case OP_POW:
        result = pow(a, b);
        break;
    default:
        result = 0.0;
        break;
    }
    return result;
}

/**
 * @brief 단일 입력 공학용 함수 계산
 * 
 * @param a 입력값
 * @param op 연산자
 * @return double 결과
 * 
 * 스택 사용량: 삼각함수 및 로그, 지수 함수 내부 사용량 있음.
 */
static double calculate_single(const double a, const Operator op)
{
    double result;
    result = 0.0;

    switch (op) {
    case OP_SIN:
        result = sin(a);
        break;
    case OP_COS:
        result = cos(a);
        break;
    case OP_TAN:
        result = tan(a);
        break;
    case OP_LOG:
        if (a > 0.0) {
            result = log10(a);
        } else {
            result = 0.0;
        }
        break;
    case OP_EXP:
        result = exp(a);
        break;
    default:
        result = 0.0;
        break;
    }
    return result;
}

/**
 * @brief 계산기 초기화
 * 
 * 스택 사용량: 로컬변수 없음, strcpy 내부 메모리 접근 최소
 */
static void reset_calc(void)
{
    calc.first = 0.0;
    calc.second = 0.0;
    calc.op = OP_NONE;
    calc.state = INPUT_FIRST;
    /* 0으로 초기화 display string, 널 종료 포함 */
    (void)strcpy(calc.display, "0");
}

/**
 * @brief display 문자열에 숫자 추가
 *  
 * 스택 사용량: strlen, 로컬변수 size_t len 8바이트 (64bit 환경 기준) = 8바이트
 * buffer overflow 방지 경계 체크 포함
 *
 * @param d 추가할 숫자 문자 ('0'~'9')
 */
static void append_digit(const char d)
{
    size_t len;

    len = strlen(calc.display);
    if (len < 16U) {
        /* 첫글자가 0이면 교체 */
        if ((len == 1U) && (calc.display[0] == '0')) {
            calc.display[0] = d;
            calc.display[1] = '\0';
        } else {
            calc.display[len] = d;
            calc.display[len + 1U] = '\0';
        }
    }
}

/**
 * @brief 계산기 메인 태스크 함수
 *
 * 스택 사용량: 
 * - 지역변수: 
 *   char c (1byte) 
 *   static int digit (4 or 8바이트, 올라간다 해도 static은 데이터영역, 스택아님)
 *   char buf[17] (17 bytes)
 * 총 약 18바이트 + 내부 lcd 함수, snprintf 호출 스택 내부량 별도.
 */
void app_main(void)
{
    int ret_gpio_config;
    static int digit = 0;
    char c;
    double res;
    char buf[17];

    ESP_LOGI(TAG, "Calculator Start");

    /* LCD 초기화 */
    lcd_init_custom();

    /* 버튼 GPIO 초기화 */
    const gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = ((uint64_t)1U << BUTTON_PIN_NUM) | ((uint64_t)1U << BUTTON_PIN_OP) |
                        ((uint64_t)1U << BUTTON_PIN_EQ) | ((uint64_t)1U << BUTTON_PIN_CLR),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };

    ret_gpio_config = gpio_config(&io_conf);
    if (ret_gpio_config != 0) {
        ESP_LOGE(TAG, "GPIO config failed");
        /* Error handling: Loop indefinitely */
        for (;; ) {
            vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
        }
    }

    reset_calc();
    lcd_display_line(calc.display, 0U);
    lcd_display_line("", 1U);

    for (;;) {
        /* 숫자 입력 단일 버튼 처리 (0~9 순환) */
        if (button_pressed((gpio_num_t)BUTTON_PIN_NUM) != 0) {
            c = (char)('0' + digit);
            digit = (digit + 1) % 10;

            if ((calc.state == INPUT_FIRST) || (calc.state == SHOW_RESULT)) {
                if (calc.state == SHOW_RESULT) {
                    reset_calc();
                }
                append_digit(c);
                calc.first = str_to_double(calc.display);
                lcd_display_line(calc.display, 0U);
            } else if (calc.state == INPUT_SECOND) {
                append_digit(c);
                calc.second = str_to_double(calc.display);
                lcd_display_line(calc.display, 0U);
            }
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        /* 연산자 버튼 처리 (임시: OP_ADD 고정) */
        if (button_pressed((gpio_num_t)BUTTON_PIN_OP) != 0) {
            switch (calc.state) {
            case INPUT_FIRST:
                calc.op = OP_ADD; /* 임시: 실제 버튼에 맞게 변경해야 함 */
                calc.state = INPUT_SECOND;
                (void)strcpy(calc.display, "0");
                lcd_display_line("+", 1U);
                lcd_display_line(calc.display, 0U);
                break;
            case INPUT_SECOND:
                /* 중복 입력 처리 간략화를 위해 임시 처리 */
                res = calculate(calc.first, calc.second, calc.op);
                calc.first = res;
                calc.op = OP_ADD; /* 임시 */
                calc.second = 0.0;
                calc.state = INPUT_SECOND;
                (void)strcpy(calc.display, "0");
                (void)snprintf(buf, sizeof(buf), "=%.8g", res);
                lcd_display_line(buf, 1U);
                lcd_display_line(calc.display, 0U);
                break;
            case SHOW_RESULT:
                calc.op = OP_ADD; /* 임시 */
                calc.state = INPUT_SECOND;
                (void)strcpy(calc.display, "0");
                lcd_display_line("+", 1U);
                lcd_display_line(calc.display, 0U);
                break;
            case INPUT_OPERATOR:
            default:
                /* 입력 상태가 모호할 경우 초기화 */
                reset_calc();
                lcd_display_line(calc.display, 0U);
                lcd_display_line("", 1U);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        /* '=' 버튼 처리 */
        if (button_pressed((gpio_num_t)BUTTON_PIN_EQ) != 0) {
            if (calc.state == INPUT_SECOND) {
                res = 0.0;
                if (calc.op == OP_NONE) {
                    res = calc.first;
                } else if ((calc.op == OP_SIN) || (calc.op == OP_COS) || (calc.op == OP_TAN) ||
                           (calc.op == OP_LOG) || (calc.op == OP_EXP)) {
                    res = calculate_single(calc.first, calc.op);
                } else {
                    res = calculate(calc.first, calc.second, calc.op);
                }
                calc.first = res;
                calc.second = 0.0;
                calc.state = SHOW_RESULT;

                (void)snprintf(buf, sizeof(buf), "%.8g", res);
                /* strncpy로 복사하되 null 보장 */
                (void)strncpy(calc.display, buf, sizeof(calc.display) - 1U);
                calc.display[sizeof(calc.display) - 1U] = '\0';

                lcd_display_line(calc.display, 0U);
                lcd_display_line("Result", 1U);
            }
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        /* Clear 버튼 처리 */
        if (button_pressed((gpio_num_t)BUTTON_PIN_CLR) != 0) {
            reset_calc();
            lcd_display_line(calc.display, 0U);
            lcd_display_line("", 1U);
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
}