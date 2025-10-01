#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
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

#define LCD_RS  5U
#define LCD_EN  6U
#define LCD_D4  7U
#define LCD_D5  8U
#define LCD_D6  9U
#define LCD_D7  10U

#define BUTTON_DEBOUNCE_DELAY_MS 50U
#define BUTTON_HOLD_DELAY_MS 10U
#define BUTTON_REPEAT_DELAY_MS 300U
#define MAIN_LOOP_DELAY_MS 10U

static const char * const TAG = "Calc";

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
    volatile char display[17]; /* 16 chars + null-terminator */
    volatile double first;
    volatile double second;
    volatile Operator op;
    volatile CalcState state;
} Calculator;

static Calculator calc;
static LCD_Handle_t lcd;

/* 초기 LCD 설정 및 초기화 */
static void lcd_init_custom(void)
{
    lcd = lcd_create(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
    assert(lcd != NULL);
    lcd_init(lcd, 16U, 2U);
    lcd_clear(lcd);
}

/* LCD 한 줄에 문자열 출력 */
static void lcd_display_line(const char* str, uint8_t line)
{
    assert(lcd != NULL);
    assert(line < 2U);
    assert(str != NULL);

    lcd_set_cursor(lcd, 0U, line);
    lcd_print(lcd, str);
}

/* 버튼 눌림 디바운스 처리 */
static bool button_pressed(const gpio_num_t pin)
{
    assert(pin <= GPIO_NUM_MAX);

    if (gpio_get_level(pin) == 0) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_DELAY_MS));
        if (gpio_get_level(pin) == 0) {
            TickType_t hold_ticks = pdMS_TO_TICKS(BUTTON_HOLD_DELAY_MS);
            while (gpio_get_level(pin) == 0) {
                vTaskDelay(hold_ticks);
            }
            return true;
        }
    }
    return false;
}

/* 문자열을 double로 변환 */
static double str_to_double(const char * const str)
{
    assert(str != NULL);
    return atof(str);
}

/* 두 숫자 연산 처리 */
static double calculate(const double a, const double b, const Operator op)
{
    switch (op) {
        case OP_ADD:
            if ((b > 0.0) && (a > DBL_MAX - b)) return 0.0; /* overflow */
            if ((b < 0.0) && (a < -DBL_MAX - b)) return 0.0; /* underflow */
            return a + b;
        case OP_SUB:
            if ((b < 0.0) && (a > DBL_MAX + b)) return 0.0; /* overflow */
            if ((b > 0.0) && (a < -DBL_MAX + b)) return 0.0; /* underflow */
            return a - b;
        case OP_MUL:
            if (a != 0.0 && (fabs(b) > DBL_MAX / fabs(a))) return 0.0; /* overflow */
            return a * b;
        case OP_DIV:
            if (b == 0.0) return 0.0;
            return a / b;
        case OP_POW:
            if ((a == 0.0) && (b <= 0.0)) return 0.0;
            return pow(a, b);
        default:
            return 0.0;
    }
}

/* 단일 입력 공학용 함수 계산 */
static double calculate_single(const double a, const Operator op)
{
    switch (op) {
        case OP_SIN:
            return sin(a);
        case OP_COS:
            return cos(a);
        case OP_TAN:
            return tan(a);
        case OP_LOG:
            if (a <= 0.0) return 0.0;
            return log10(a);
        case OP_EXP:
            if (a > 700.0) return 0.0;
            if (a < -700.0) return 0.0;
            return exp(a);
        default:
            return 0.0;
    }
}

/* 계산기 상태 초기화 */
static void reset_calc(void)
{
    taskENTER_CRITICAL();
    calc.first = 0.0;
    calc.second = 0.0;
    calc.op = OP_NONE;
    calc.state = INPUT_FIRST;
    strncpy((char *)calc.display, "0", sizeof(calc.display));
    calc.display[sizeof(calc.display) - 1U] = '\0';
    taskEXIT_CRITICAL();
}

/* 숫자 문자 붙이기, 최대 16자 */
static void append_digit(const char d)
{
    assert(d >= '0' && d <= '9');

    taskENTER_CRITICAL();
    size_t len = strnlen((const char *)calc.display, sizeof(calc.display));
    if (len < 16U) {
        if ((len == 1U) && (((char)calc.display[0]) == '0')) {
            calc.display[0] = d;
            calc.display[1] = '\0';
        } else {
            calc.display[len] = d;
            calc.display[len + 1U] = '\0';
        }
    }
    taskEXIT_CRITICAL();
}

/* 연산자 버튼 처리 함수 */
static void handle_operator_button(void)
{
    double res;
    char buf[17];
    const Operator new_op = OP_ADD;

    taskENTER_CRITICAL();
    CalcState state = calc.state;
    double first = calc.first;
    double second = calc.second;
    Operator op = calc.op;
    taskEXIT_CRITICAL();

    switch (state) {
        case INPUT_FIRST:
            taskENTER_CRITICAL();
            calc.op = new_op;
            calc.state = INPUT_SECOND;
            strncpy((char *)calc.display, "0", sizeof(calc.display));
            calc.display[sizeof(calc.display) - 1U] = '\0';
            taskEXIT_CRITICAL();

            lcd_display_line("+", 1U);
            lcd_display_line((const char *)calc.display, 0U);
            break;
        case INPUT_SECOND:
            res = calculate(first, second, op);
            taskENTER_CRITICAL();
            calc.first = res;
            calc.op = new_op;
            calc.second = 0.0;
            calc.state = INPUT_SECOND;
            strncpy((char *)calc.display, "0", sizeof(calc.display));
            calc.display[sizeof(calc.display) - 1U] = '\0';
            taskEXIT_CRITICAL();

            int n = snprintf(buf, sizeof(buf), "=%.8g", res);
            assert(n > 0 && n < (int)sizeof(buf));
            lcd_display_line(buf, 1U);
            lcd_display_line((const char *)calc.display, 0U);
            break;
        case SHOW_RESULT:
            taskENTER_CRITICAL();
            calc.op = new_op;
            calc.state = INPUT_SECOND;
            strncpy((char *)calc.display, "0", sizeof(calc.display));
            calc.display[sizeof(calc.display) - 1U] = '\0';
            taskEXIT_CRITICAL();

            lcd_display_line("+", 1U);
            lcd_display_line((const char *)calc.display, 0U);
            break;
        case INPUT_OPERATOR:
        default:
            reset_calc();
            lcd_display_line((const char *)calc.display, 0U);
            lcd_display_line("", 1U);
            break;
    }
}

/* '=' 버튼 처리 함수 */
static void handle_equal_button(void)
{
    double res;
    char buf[17];

    taskENTER_CRITICAL();
    CalcState state = calc.state;
    Operator op = calc.op;
    double first = calc.first;
    double second = calc.second;
    taskEXIT_CRITICAL();

    if (state != INPUT_SECOND) {
        return;
    }

    if (op == OP_NONE) {
        res = first;
    } else if ((op == OP_SIN) || (op == OP_COS) || (op == OP_TAN) ||
               (op == OP_LOG) || (op == OP_EXP)) {
        res = calculate_single(first, op);
    } else {
        res = calculate(first, second, op);
    }

    taskENTER_CRITICAL();
    calc.first = res;
    calc.second = 0.0;
    calc.state = SHOW_RESULT;
    int n = snprintf(buf, sizeof(buf), "%.8g", res);
    assert(n > 0 && n < (int)sizeof(buf));
    strncpy((char *)calc.display, buf, sizeof(calc.display) - 1U);
    calc.display[sizeof(calc.display) - 1U] = '\0';
    taskEXIT_CRITICAL();

    lcd_display_line((const char *)calc.display, 0U);
    lcd_display_line("Result", 1U);
}

/* 숫자 버튼 처리 함수 */
static void handle_number_button(char c)
{
    assert(c >= '0' && c <= '9');

    taskENTER_CRITICAL();
    CalcState state = calc.state;
    taskEXIT_CRITICAL();

    if ((state == INPUT_FIRST) || (state == SHOW_RESULT)) {
        if (state == SHOW_RESULT) {
            reset_calc();
        }
        append_digit(c);
        taskENTER_CRITICAL();
        double val = str_to_double((const char *)calc.display);
        calc.first = val;
        taskEXIT_CRITICAL();

        lcd_display_line((const char *)calc.display, 0U);
    } else if (state == INPUT_SECOND) {
        append_digit(c);
        taskENTER_CRITICAL();
        double val = str_to_double((const char *)calc.display);
        calc.second = val;
        taskEXIT_CRITICAL();

        lcd_display_line((const char *)calc.display, 0U);
    }
}

/* Clear 버튼 처리 함수 */
static void handle_clear_button(void)
{
    reset_calc();
    lcd_display_line((const char *)calc.display, 0U);
    lcd_display_line("", 1U);
}

/* GPIO 초기화 및 버튼 핀 설정 */
static void gpio_buttons_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = ((uint64_t)1U << BUTTON_PIN_NUM) | ((uint64_t)1U << BUTTON_PIN_OP) |
                        ((uint64_t)1U << BUTTON_PIN_EQ) | ((uint64_t)1U << BUTTON_PIN_CLR),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };

    int ret = gpio_config(&io_conf);
    if (ret != 0) {
        ESP_LOGE(TAG, "GPIO config failed");
        assert(false);
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
        }
    }
}

/* 메인 계산기 루프 처리 */
static void calculator_loop(void)
{
    static volatile int digit = 0;
    char c;

    for (;;) {
        if (button_pressed((gpio_num_t)BUTTON_PIN_NUM)) {
            c = (char)('0' + digit);
            digit = (digit + 1) % 10;
            handle_number_button(c);
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        if (button_pressed((gpio_num_t)BUTTON_PIN_OP)) {
            handle_operator_button();
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        if (button_pressed((gpio_num_t)BUTTON_PIN_EQ)) {
            handle_equal_button();
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        if (button_pressed((gpio_num_t)BUTTON_PIN_CLR)) {
            handle_clear_button();
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Calculator Start");
    lcd_init_custom();
    gpio_buttons_init();
    reset_calc();

    lcd_display_line((const char *)calc.display, 0U);
    lcd_display_line("", 1U);

    calculator_loop();
}