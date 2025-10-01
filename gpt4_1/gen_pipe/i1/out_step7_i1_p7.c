#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>
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

/**
 * @brief Error codes for calculator operations and hardware interactions.
 */
typedef enum {
    ERR_OK = 0,           /**< Operation succeeded */
    ERR_FAIL = -1,        /**< General failure */
    ERR_INVALID_ARG = -2, /**< Invalid argument passed */
    ERR_OVERFLOW = -3,    /**< Numeric overflow */
    ERR_DIV_ZERO = -4,    /**< Division by zero */
    ERR_UNDERFLOW = -5,   /**< Numeric underflow */
    ERR_LCD_FAIL = -6,    /**< LCD related failure */
    ERR_GPIO_FAIL = -7,   /**< GPIO related failure */
} ErrorCode;

/**
 * @brief Calculator input processing states.
 */
typedef enum {
    INPUT_FIRST = 0,   /**< Inputting first operand */
    INPUT_OPERATOR,    /**< Operator input */
    INPUT_SECOND,      /**< Inputting second operand */
    SHOW_RESULT       /**< Showing result */
} CalcState;

/**
 * @brief Supported calculator operators.
 */
typedef enum {
    OP_NONE = 0, /**< No operation */
    OP_ADD,      /**< Addition (+) */
    OP_SUB,      /**< Subtraction (-) */
    OP_MUL,      /**< Multiplication (*) */
    OP_DIV,      /**< Division (/) */
    OP_SIN,      /**< Sine */
    OP_COS,      /**< Cosine */
    OP_TAN,      /**< Tangent */
    OP_LOG,      /**< Logarithm base 10 */
    OP_EXP,      /**< Exponential */
    OP_POW       /**< Power */
} Operator;

/**
 * @brief Calculator data structure maintaining state and display buffer.
 */
typedef struct {
    volatile char display[17]; /**< Display buffer (16 chars + null terminator) */
    volatile double first;     /**< First operand */
    volatile double second;    /**< Second operand */
    volatile Operator op;      /**< Current operator */
    volatile CalcState state;  /**< Current calculator state */
} Calculator;

static Calculator calc; /**< Calculator instance */
static LCD_Handle_t lcd; /**< LCD handle */

/**
 * @brief Initialize and configure the LCD module.
 *
 * @retval ERR_OK On success.
 * @retval ERR_LCD_FAIL On failure to create or initialize LCD.
 */
static ErrorCode lcd_init_custom(void)
{
    lcd = lcd_create(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
    if (lcd == NULL) {
        ESP_LOGE(TAG, "lcd_create failed");
        return ERR_LCD_FAIL;
    }
    if (lcd_init(lcd, 16U, 2U) != 0) {
        ESP_LOGE(TAG, "lcd_init failed");
        return ERR_LCD_FAIL;
    }
    if (lcd_clear(lcd) != 0) {
        ESP_LOGE(TAG, "lcd_clear failed");
        return ERR_LCD_FAIL;
    }
    return ERR_OK;
}

/**
 * @brief Display a string on a specific line of the LCD.
 *
 * @param[in] str Null-terminated string to display.
 * @param[in] line Line number to display the string on (0 or 1).
 *
 * @retval ERR_OK On success.
 * @retval ERR_INVALID_ARG On invalid parameters.
 * @retval ERR_LCD_FAIL On failure communicating with LCD.
 */
static ErrorCode lcd_display_line(const char* str, uint8_t line)
{
    if (lcd == NULL || str == NULL || line >= 2U) {
        ESP_LOGE(TAG, "lcd_display_line invalid arguments");
        return ERR_INVALID_ARG;
    }

    if (lcd_set_cursor(lcd, 0U, line) != 0) {
        ESP_LOGE(TAG, "lcd_set_cursor failed");
        return ERR_LCD_FAIL;
    }
    if (lcd_print(lcd, str) != 0) {
        ESP_LOGE(TAG, "lcd_print failed");
        return ERR_LCD_FAIL;
    }

    return ERR_OK;
}

/**
 * @brief Debounce and detect if a button (GPIO pin) is pressed.
 *
 * This function waits for the debounce delay and confirms button press.
 *
 * @param[in] pin GPIO pin number for the button.
 * @param[out] pressed Pointer to boolean where the result is stored.
 *
 * @retval ERR_OK On success.
 * @retval ERR_INVALID_ARG On invalid parameters.
 */
static ErrorCode button_pressed(const gpio_num_t pin, bool *pressed)
{
    if (pressed == NULL) {
        return ERR_INVALID_ARG;
    }
    if (pin > GPIO_NUM_MAX) {
        return ERR_INVALID_ARG;
    }

    *pressed = false;

    if (gpio_get_level(pin) == 0) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_DELAY_MS));
        if (gpio_get_level(pin) == 0) {
            TickType_t hold_ticks = pdMS_TO_TICKS(BUTTON_HOLD_DELAY_MS);
            while (gpio_get_level(pin) == 0) {
                vTaskDelay(hold_ticks);
            }
            *pressed = true;
        }
    }
    return ERR_OK;
}

/**
 * @brief Convert string to double, validating full conversion.
 *
 * @param[in] str Null-terminated input string.
 * @param[out] out_val Pointer to double to store converted value.
 *
 * @retval ERR_OK On successful conversion.
 * @retval ERR_INVALID_ARG On invalid input or conversion failure.
 */
static ErrorCode str_to_double(const char * const str, double *out_val)
{
    if (str == NULL || out_val == NULL) {
        return ERR_INVALID_ARG;
    }

    char *endptr = NULL;
    double val = strtod(str, &endptr);
    if (endptr == str || *endptr != '\0') {
        return ERR_INVALID_ARG;
    }
    *out_val = val;
    return ERR_OK;
}

/**
 * @brief Perform binary arithmetic operation on two doubles.
 *
 * @param[in] a First operand.
 * @param[in] b Second operand.
 * @param[in] op Operator.
 * @param[out] res Result pointer.
 *
 * @retval ERR_OK On success.
 * @retval ERR_INVALID_ARG On invalid operation.
 * @retval ERR_OVERFLOW On overflow.
 * @retval ERR_UNDERFLOW On underflow.
 * @retval ERR_DIV_ZERO On division by zero.
 */
static ErrorCode calculate(const double a, const double b, const Operator op, double *res)
{
    if (res == NULL) {
        return ERR_INVALID_ARG;
    }

    switch (op) {
        case OP_ADD:
            if ((b > 0.0) && (a > DBL_MAX - b)) return ERR_OVERFLOW;
            if ((b < 0.0) && (a < -DBL_MAX - b)) return ERR_UNDERFLOW;
            *res = a + b;
            break;
        case OP_SUB:
            if ((b < 0.0) && (a > DBL_MAX + b)) return ERR_OVERFLOW;
            if ((b > 0.0) && (a < -DBL_MAX + b)) return ERR_UNDERFLOW;
            *res = a - b;
            break;
        case OP_MUL:
            if (a != 0.0 && (fabs(b) > DBL_MAX / fabs(a))) return ERR_OVERFLOW;
            *res = a * b;
            break;
        case OP_DIV:
            if (b == 0.0) return ERR_DIV_ZERO;
            *res = a / b;
            break;
        case OP_POW:
            if ((a == 0.0) && (b <= 0.0)) return ERR_INVALID_ARG;
            *res = pow(a, b);
            break;
        default:
            return ERR_INVALID_ARG;
    }
    return ERR_OK;
}

/**
 * @brief Perform single-operand scientific calculation.
 *
 * @param[in] a Operand.
 * @param[in] op Operator.
 * @param[out] res Result pointer.
 *
 * @retval ERR_OK On success.
 * @retval ERR_INVALID_ARG On invalid operator or domain error.
 * @retval ERR_OVERFLOW On numeric overflow.
 */
static ErrorCode calculate_single(const double a, const Operator op, double *res)
{
    if (res == NULL) {
        return ERR_INVALID_ARG;
    }

    switch (op) {
        case OP_SIN:
            *res = sin(a);
            break;
        case OP_COS:
            *res = cos(a);
            break;
        case OP_TAN:
            *res = tan(a);
            break;
        case OP_LOG:
            if (a <= 0.0) return ERR_INVALID_ARG;
            *res = log10(a);
            break;
        case OP_EXP:
            if (a > 700.0 || a < -700.0) return ERR_OVERFLOW;
            *res = exp(a);
            break;
        default:
            return ERR_INVALID_ARG;
    }
    return ERR_OK;
}

/**
 * @brief Reset calculator to initial state.
 *
 * Clears operands, operator, state and updates the display buffer.
 */
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

/**
 * @brief Append digit character to the current display string, max 16 chars.
 *
 * @param[in] d Digit character ('0' to '9').
 */
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

/**
 * @brief Handle operator button press ('+').
 *
 * This example supports addition operator only for demonstration.
 * Updates calculator state and LCD display accordingly.
 *
 * @retval ERR_OK On success.
 * @retval ERR_LCD_FAIL On LCD display failure.
 * @retval ERR_FAIL On calculation failure.
 */
static ErrorCode handle_operator_button(void)
{
    double res;
    ErrorCode err = ERR_OK;
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

            if (lcd_display_line("+", 1U) != ERR_OK ||
                lcd_display_line((const char *)calc.display, 0U) != ERR_OK) {
                ESP_LOGW(TAG, "LCD display line failed");
                err = ERR_LCD_FAIL;
            }
            break;
        case INPUT_SECOND:
            err = calculate(first, second, op, &res);
            if (err != ERR_OK) {
                ESP_LOGW(TAG, "Calculation error %d", err);
                return err;
            }
            taskENTER_CRITICAL();
            calc.first = res;
            calc.op = new_op;
            calc.second = 0.0;
            calc.state = INPUT_SECOND;
            strncpy((char *)calc.display, "0", sizeof(calc.display));
            calc.display[sizeof(calc.display) - 1U] = '\0';
            taskEXIT_CRITICAL();

            int n = snprintf(buf, sizeof(buf), "=%.8g", res);
            if (n <= 0 || (size_t)n >= sizeof(buf)) {
                ESP_LOGW(TAG, "snprintf error");
                return ERR_FAIL;
            }
            if (lcd_display_line(buf, 1U) != ERR_OK ||
                lcd_display_line((const char *)calc.display, 0U) != ERR_OK) {
                ESP_LOGW(TAG, "LCD display line failed");
                err = ERR_LCD_FAIL;
            }
            break;
        case SHOW_RESULT:
            taskENTER_CRITICAL();
            calc.op = new_op;
            calc.state = INPUT_SECOND;
            strncpy((char *)calc.display, "0", sizeof(calc.display));
            calc.display[sizeof(calc.display) - 1U] = '\0';
            taskEXIT_CRITICAL();

            if (lcd_display_line("+", 1U) != ERR_OK ||
                lcd_display_line((const char *)calc.display, 0U) != ERR_OK) {
                ESP_LOGW(TAG, "LCD display line failed");
                err = ERR_LCD_FAIL;
            }
            break;
        case INPUT_OPERATOR:
        default:
            reset_calc();
            /* On invalid state, reset and clear display, no error reported as recoverable */
            lcd_display_line((const char *)calc.display, 0U);
            lcd_display_line("", 1U);
            break;
    }
    return err;
}

/**
 * @brief Handle '=' button press, completing the calculation.
 *
 * Computes the result based on the current operator and operands,
 * updates display and calculator state.
 *
 * @retval ERR_OK On success.
 * @retval ERR_FAIL If '=' pressed in wrong state or snprintf error.
 * @retval ERR_INVALID_ARG On invalid operator or input.
 * @retval ERR_DIV_ZERO On division by zero.
 * @retval ERR_OVERFLOW On numeric overflow.
 */
static ErrorCode handle_equal_button(void)
{
    double res;
    ErrorCode err = ERR_OK;
    char buf[17];

    taskENTER_CRITICAL();
    CalcState state = calc.state;
    Operator op = calc.op;
    double first = calc.first;
    double second = calc.second;
    taskEXIT_CRITICAL();

    if (state != INPUT_SECOND) {
        return ERR_FAIL; /* Fail safe: '=' can only be pressed in INPUT_SECOND state */
    }

    if (op == OP_NONE) {
        res = first;
    } else if ((op == OP_SIN) || (op == OP_COS) || (op == OP_TAN) ||
               (op == OP_LOG) || (op == OP_EXP)) {
        err = calculate_single(first, op, &res);
        if (err != ERR_OK) {
            ESP_LOGW(TAG, "Single operand calculation error %d", err);
            return err;
        }
    } else {
        err = calculate(first, second, op, &res);
        if (err != ERR_OK) {
            ESP_LOGW(TAG, "Calculation error %d", err);
            return err;
        }
    }

    taskENTER_CRITICAL();
    calc.first = res;
    calc.second = 0.0;
    calc.state = SHOW_RESULT;
    int n = snprintf(buf, sizeof(buf), "%.8g", res);
    if (n <= 0 || (size_t)n >= sizeof(buf)) {
        ESP_LOGW(TAG, "snprintf error");
        taskEXIT_CRITICAL();
        return ERR_FAIL;
    }
    strncpy((char *)calc.display, buf, sizeof(calc.display) - 1U);
    calc.display[sizeof(calc.display) - 1U] = '\0';
    taskEXIT_CRITICAL();

    err = lcd_display_line((const char *)calc.display, 0U);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "LCD display line failed");
        return err;
    }
    err = lcd_display_line("Result", 1U);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "LCD display line failed");
    }
    return err;
}

/**
 * @brief Handle numeric button input.
 *
 * Appends the digit character to display buffer and updates operand accordingly.
 *
 * @param[in] c Digit character ('0' to '9').
 *
 * @retval ERR_OK On success.
 * @retval ERR_FAIL On unknown calculator state.
 * @retval ERR_INVALID_ARG On string to double conversion failure.
 * @retval ERR_LCD_FAIL On LCD display failure.
 */
static ErrorCode handle_number_button(char c)
{
    ErrorCode err;
    assert(c >= '0' && c <= '9');

    taskENTER_CRITICAL();
    CalcState state = calc.state;
    taskEXIT_CRITICAL();

    if ((state == INPUT_FIRST) || (state == SHOW_RESULT)) {
        if (state == SHOW_RESULT) {
            reset_calc();
        }
        append_digit(c);
        double val = 0;
        err = str_to_double((const char *)calc.display, &val);
        if (err != ERR_OK) {
            ESP_LOGW(TAG, "str_to_double conversion failed");
            return err;
        }
        taskENTER_CRITICAL();
        calc.first = val;
        taskEXIT_CRITICAL();

        err = lcd_display_line((const char *)calc.display, 0U);
        if (err != ERR_OK) {
            ESP_LOGW(TAG, "LCD display line failed");
            return err;
        }
    } else if (state == INPUT_SECOND) {
        append_digit(c);
        double val = 0;
        err = str_to_double((const char *)calc.display, &val);
        if (err != ERR_OK) {
            ESP_LOGW(TAG, "str_to_double conversion failed");
            return err;
        }
        taskENTER_CRITICAL();
        calc.second = val;
        taskEXIT_CRITICAL();

        err = lcd_display_line((const char *)calc.display, 0U);
        if (err != ERR_OK) {
            ESP_LOGW(TAG, "LCD display line failed");
            return err;
        }
    } else {
        return ERR_FAIL; /* Unknown state */
    }
    return ERR_OK;
}

/**
 * @brief Handle 'Clear' button press.
 *
 * Resets calculator state and clears LCD display.
 *
 * @retval ERR_OK On success.
 * @retval ERR_LCD_FAIL On LCD display failure.
 */
static ErrorCode handle_clear_button(void)
{
    reset_calc();
    ErrorCode err = lcd_display_line((const char *)calc.display, 0U);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "LCD display line failed");
        return err;
    }
    err = lcd_display_line("", 1U);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "LCD display line failed");
    }
    return err;
}

/**
 * @brief Initialize GPIO pins for buttons as inputs with pull-ups.
 *
 * @retval ERR_OK On success.
 * @retval ERR_GPIO_FAIL On GPIO configuration failure.
 */
static ErrorCode gpio_buttons_init(void)
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
        ESP_LOGE(TAG, "GPIO config failed with code %d", ret);
        return ERR_GPIO_FAIL;
    }
    return ERR_OK;
}

/**
 * @brief Main calculator input processing loop.
 *
 * Continuously polls buttons, handles inputs, and updates calculator state and LCD.
 */
static void calculator_loop(void)
{
    static volatile int digit = 0;
    char c;
    bool is_pressed;
    ErrorCode err;

    for (;;) {
        err = button_pressed((gpio_num_t)BUTTON_PIN_NUM, &is_pressed);
        if (err == ERR_OK && is_pressed) {
            c = (char)('0' + digit);
            digit = (digit + 1) % 10;
            err = handle_number_button(c);
            if (err != ERR_OK) {
                ESP_LOGW(TAG, "handle_number_button failed with error %d", err);
            }
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        err = button_pressed((gpio_num_t)BUTTON_PIN_OP, &is_pressed);
        if (err == ERR_OK && is_pressed) {
            err = handle_operator_button();
            if (err != ERR_OK) {
                ESP_LOGW(TAG, "handle_operator_button failed with error %d", err);
            }
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        err = button_pressed((gpio_num_t)BUTTON_PIN_EQ, &is_pressed);
        if (err == ERR_OK && is_pressed) {
            err = handle_equal_button();
            if (err != ERR_OK) {
                ESP_LOGW(TAG, "handle_equal_button failed with error %d", err);
            }
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        err = button_pressed((gpio_num_t)BUTTON_PIN_CLR, &is_pressed);
        if (err == ERR_OK && is_pressed) {
            err = handle_clear_button();
            if (err != ERR_OK) {
                ESP_LOGW(TAG, "handle_clear_button failed with error %d", err);
            }
            vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
}

/**
 * @brief Application entry point.
 *
 * Initializes LCD, buttons, and starts the calculator loop.
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Calculator Start");
    ErrorCode err;

    err = lcd_init_custom();
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "LCD init failed, system halt");
        /* Fail-safe halt */
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
        }
    }

    err = gpio_buttons_init();
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "GPIO init failed, system halt");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
        }
    }

    reset_calc();

    err = lcd_display_line((const char *)calc.display, 0U);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "LCD display line failed at start");
    }
    err = lcd_display_line("", 1U);
    if (err != ERR_OK) {
        ESP_LOGW(TAG, "LCD display line failed at start");
    }

    calculator_loop();
}