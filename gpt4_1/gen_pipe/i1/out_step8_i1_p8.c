#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <assert.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

/* LCD 16x2 관련 헤더 (예제용, 실제 보드 환경에 맞게 변경 필요) */
#include "lcd.h"

#define BUTTON_PIN_NUM      0U /* 버튼 신호 입력 핀 예, 실제 핀번호 보드에 맞게 변경 */
#define BUTTON_PIN_OP       1U /* 연산자 입력 버튼 핀 */
#define BUTTON_PIN_EQ       2U /* '=' 버튼 핀 */
#define BUTTON_PIN_CLR      3U /* 'Clear' 버튼 핀 */

#define LCD_RS              5U
#define LCD_EN              6U
#define LCD_D4              7U
#define LCD_D5              8U
#define LCD_D6              9U
#define LCD_D7              10U

#define BUTTON_DEBOUNCE_DELAY_MS 50U
#define BUTTON_HOLD_DELAY_MS     10U
#define BUTTON_REPEAT_DELAY_MS   300U
#define MAIN_LOOP_DELAY_MS       10U

static const char *const kTag = "Calc";

/**
 * @brief Error codes for calculator operations and hardware interactions.
 */
typedef enum {
  ErrOk = 0,           /**< Operation succeeded */
  ErrFail = -1,        /**< General failure */
  ErrInvalidArg = -2,  /**< Invalid argument passed */
  ErrOverflow = -3,    /**< Numeric overflow */
  ErrDivZero = -4,     /**< Division by zero */
  ErrUnderflow = -5,   /**< Numeric underflow */
  ErrLcdFail = -6,     /**< LCD related failure */
  ErrGpioFail = -7     /**< GPIO related failure */
} ErrorCode;

/**
 * @brief Calculator input processing states.
 */
typedef enum {
  InputFirst = 0,  /**< Inputting first operand */
  InputOperator,   /**< Operator input */
  InputSecond,     /**< Inputting second operand */
  ShowResult      /**< Showing result */
} CalcState;

/**
 * @brief Supported calculator operators.
 */
typedef enum {
  OpNone = 0, /**< No operation */
  OpAdd,      /**< Addition (+) */
  OpSub,      /**< Subtraction (-) */
  OpMul,      /**< Multiplication (*) */
  OpDiv,      /**< Division (/) */
  OpSin,      /**< Sine */
  OpCos,      /**< Cosine */
  OpTan,      /**< Tangent */
  OpLog,      /**< Logarithm base 10 */
  OpExp,      /**< Exponential */
  OpPow       /**< Power */
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

static Calculator g_calc;    /**< Calculator instance */
static LCD_Handle_t g_lcd;   /**< LCD handle */

/**
 * @brief Initialize and configure the LCD module.
 *
 * @retval ErrOk On success.
 * @retval ErrLcdFail On failure to create or initialize LCD.
 */
static ErrorCode LcdInit(void) {
  g_lcd = lcd_create(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
  if (g_lcd == NULL) {
    ESP_LOGE(kTag, "lcd_create failed");
    return ErrLcdFail;
  }
  if (lcd_init(g_lcd, 16U, 2U) != 0) {
    ESP_LOGE(kTag, "lcd_init failed");
    return ErrLcdFail;
  }
  if (lcd_clear(g_lcd) != 0) {
    ESP_LOGE(kTag, "lcd_clear failed");
    return ErrLcdFail;
  }
  return ErrOk;
}

/**
 * @brief Display a string on a specific line of the LCD.
 *
 * @param[in] str Null-terminated string to display.
 * @param[in] line Line number to display the string on (0 or 1).
 *
 * @retval ErrOk On success.
 * @retval ErrInvalidArg On invalid parameters.
 * @retval ErrLcdFail On failure communicating with LCD.
 */
static ErrorCode LcdDisplayLine(const char *str, uint8_t line) {
  if (g_lcd == NULL || str == NULL || line >= 2U) {
    ESP_LOGE(kTag, "lcd_display_line invalid arguments");
    return ErrInvalidArg;
  }

  if (lcd_set_cursor(g_lcd, 0U, line) != 0) {
    ESP_LOGE(kTag, "lcd_set_cursor failed");
    return ErrLcdFail;
  }
  if (lcd_print(g_lcd, str) != 0) {
    ESP_LOGE(kTag, "lcd_print failed");
    return ErrLcdFail;
  }

  return ErrOk;
}

/**
 * @brief Debounce and detect if a button (GPIO pin) is pressed.
 *
 * This function waits for the debounce delay and confirms button press.
 *
 * @param[in] pin GPIO pin number for the button.
 * @param[out] pressed Pointer to boolean where the result is stored.
 *
 * @retval ErrOk On success.
 * @retval ErrInvalidArg On invalid parameters.
 */
static ErrorCode ButtonPressed(gpio_num_t pin, bool *pressed) {
  if (pressed == NULL) {
    return ErrInvalidArg;
  }
  if ((uint32_t)pin > (uint32_t)GPIO_NUM_MAX) {
    return ErrInvalidArg;
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
  return ErrOk;
}

/**
 * @brief Convert string to double, validating full conversion.
 *
 * @param[in] str Null-terminated input string.
 * @param[out] out_val Pointer to double to store converted value.
 *
 * @retval ErrOk On successful conversion.
 * @retval ErrInvalidArg On invalid input or conversion failure.
 */
static ErrorCode StrToDouble(const char *const str, double *out_val) {
  if (str == NULL || out_val == NULL) {
    return ErrInvalidArg;
  }

  char *end_ptr = NULL;
  double val = strtod(str, &end_ptr);
  if (end_ptr == str || *end_ptr != '\0') {
    return ErrInvalidArg;
  }
  *out_val = val;
  return ErrOk;
}

/**
 * @brief Perform binary arithmetic operation on two doubles.
 *
 * @param[in] a First operand.
 * @param[in] b Second operand.
 * @param[in] op Operator.
 * @param[out] res Result pointer.
 *
 * @retval ErrOk On success.
 * @retval ErrInvalidArg On invalid operation.
 * @retval ErrOverflow On overflow.
 * @retval ErrUnderflow On underflow.
 * @retval ErrDivZero On division by zero.
 */
static ErrorCode Calculate(double a, double b, Operator op, double *res) {
  if (res == NULL) {
    return ErrInvalidArg;
  }

  switch (op) {
    case OpAdd:
      if ((b > 0.0) && (a > DBL_MAX - b)) {
        return ErrOverflow;
      }
      if ((b < 0.0) && (a < -DBL_MAX - b)) {
        return ErrUnderflow;
      }
      *res = a + b;
      break;

    case OpSub:
      if ((b < 0.0) && (a > DBL_MAX + b)) {
        return ErrOverflow;
      }
      if ((b > 0.0) && (a < -DBL_MAX + b)) {
        return ErrUnderflow;
      }
      *res = a - b;
      break;

    case OpMul:
      if (a != 0.0 && (fabs(b) > DBL_MAX / fabs(a))) {
        return ErrOverflow;
      }
      *res = a * b;
      break;

    case OpDiv:
      if (b == 0.0) {
        return ErrDivZero;
      }
      *res = a / b;
      break;

    case OpPow:
      if ((a == 0.0) && (b <= 0.0)) {
        return ErrInvalidArg;
      }
      *res = pow(a, b);
      break;

    default:
      return ErrInvalidArg;
  }

  return ErrOk;
}

/**
 * @brief Perform single-operand scientific calculation.
 *
 * @param[in] a Operand.
 * @param[in] op Operator.
 * @param[out] res Result pointer.
 *
 * @retval ErrOk On success.
 * @retval ErrInvalidArg On invalid operator or domain error.
 * @retval ErrOverflow On numeric overflow.
 */
static ErrorCode CalculateSingle(double a, Operator op, double *res) {
  if (res == NULL) {
    return ErrInvalidArg;
  }

  switch (op) {
    case OpSin:
      *res = sin(a);
      break;
    case OpCos:
      *res = cos(a);
      break;
    case OpTan:
      *res = tan(a);
      break;
    case OpLog:
      if (a <= 0.0) {
        return ErrInvalidArg;
      }
      *res = log10(a);
      break;
    case OpExp:
      if (a > 700.0 || a < -700.0) {
        return ErrOverflow;
      }
      *res = exp(a);
      break;
    default:
      return ErrInvalidArg;
  }

  return ErrOk;
}

/**
 * @brief Reset calculator to initial state.
 *
 * Clears operands, operator, state and updates the display buffer.
 */
static void ResetCalc(void) {
  taskENTER_CRITICAL();
  g_calc.first = 0.0;
  g_calc.second = 0.0;
  g_calc.op = OpNone;
  g_calc.state = InputFirst;
  strncpy((char *)g_calc.display, "0", sizeof(g_calc.display));
  g_calc.display[sizeof(g_calc.display) - 1U] = '\0';
  taskEXIT_CRITICAL();
}

/**
 * @brief Append digit character to the current display string, max 16 chars.
 *
 * @param[in] digit Digit character ('0' to '9').
 */
static void AppendDigit(char digit) {
  assert(digit >= '0' && digit <= '9');

  taskENTER_CRITICAL();
  size_t length = strnlen((const char *)g_calc.display, sizeof(g_calc.display));
  if (length < 16U) {
    if ((length == 1U) && (((char)g_calc.display[0]) == '0')) {
      g_calc.display[0] = digit;
      g_calc.display[1] = '\0';
    } else {
      g_calc.display[length] = digit;
      g_calc.display[length + 1U] = '\0';
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
 * @retval ErrOk On success.
 * @retval ErrLcdFail On LCD display failure.
 * @retval ErrFail On calculation failure.
 */
static ErrorCode HandleOperatorButton(void) {
  double result;
  ErrorCode err = ErrOk;
  char buffer[17];
  const Operator new_op = OpAdd;

  taskENTER_CRITICAL();
  CalcState state = g_calc.state;
  double first = g_calc.first;
  double second = g_calc.second;
  Operator op = g_calc.op;
  taskEXIT_CRITICAL();

  switch (state) {
    case InputFirst:
      taskENTER_CRITICAL();
      g_calc.op = new_op;
      g_calc.state = InputSecond;
      strncpy((char *)g_calc.display, "0", sizeof(g_calc.display));
      g_calc.display[sizeof(g_calc.display) - 1U] = '\0';
      taskEXIT_CRITICAL();

      if (LcdDisplayLine("+", 1U) != ErrOk ||
          LcdDisplayLine((const char *)g_calc.display, 0U) != ErrOk) {
        ESP_LOGW(kTag, "LCD display line failed");
        err = ErrLcdFail;
      }
      break;

    case InputSecond:
      err = Calculate(first, second, op, &result);
      if (err != ErrOk) {
        ESP_LOGW(kTag, "Calculation error %d", err);
        return err;
      }
      taskENTER_CRITICAL();
      g_calc.first = result;
      g_calc.op = new_op;
      g_calc.second = 0.0;
      g_calc.state = InputSecond;
      strncpy((char *)g_calc.display, "0", sizeof(g_calc.display));
      g_calc.display[sizeof(g_calc.display) - 1U] = '\0';
      taskEXIT_CRITICAL();

      int n = snprintf(buffer, sizeof(buffer), "=%.8g", result);
      if ((n <= 0) || ((size_t)n >= sizeof(buffer))) {
        ESP_LOGW(kTag, "snprintf error");
        return ErrFail;
      }
      if (LcdDisplayLine(buffer, 1U) != ErrOk ||
          LcdDisplayLine((const char *)g_calc.display, 0U) != ErrOk) {
        ESP_LOGW(kTag, "LCD display line failed");
        err = ErrLcdFail;
      }
      break;

    case ShowResult:
      taskENTER_CRITICAL();
      g_calc.op = new_op;
      g_calc.state = InputSecond;
      strncpy((char *)g_calc.display, "0", sizeof(g_calc.display));
      g_calc.display[sizeof(g_calc.display) - 1U] = '\0';
      taskEXIT_CRITICAL();

      if (LcdDisplayLine("+", 1U) != ErrOk ||
          LcdDisplayLine((const char *)g_calc.display, 0U) != ErrOk) {
        ESP_LOGW(kTag, "LCD display line failed");
        err = ErrLcdFail;
      }
      break;

    case InputOperator:
    default:
      ResetCalc();
      /* On invalid state, reset and clear display, no error reported as recoverable */
      LcdDisplayLine((const char *)g_calc.display, 0U);
      LcdDisplayLine("", 1U);
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
 * @retval ErrOk On success.
 * @retval ErrFail If '=' pressed in wrong state or snprintf error.
 * @retval ErrInvalidArg On invalid operator or input.
 * @retval ErrDivZero On division by zero.
 * @retval ErrOverflow On numeric overflow.
 */
static ErrorCode HandleEqualButton(void) {
  double result;
  ErrorCode err = ErrOk;
  char buffer[17];

  taskENTER_CRITICAL();
  CalcState state = g_calc.state;
  Operator op = g_calc.op;
  double first = g_calc.first;
  double second = g_calc.second;
  taskEXIT_CRITICAL();

  if (state != InputSecond) {
    return ErrFail; /* '=' can only be pressed in InputSecond state */
  }

  if (op == OpNone) {
    result = first;
  } else if ((op == OpSin) || (op == OpCos) || (op == OpTan) ||
             (op == OpLog) || (op == OpExp)) {
    err = CalculateSingle(first, op, &result);
    if (err != ErrOk) {
      ESP_LOGW(kTag, "Single operand calculation error %d", err);
      return err;
    }
  } else {
    err = Calculate(first, second, op, &result);
    if (err != ErrOk) {
      ESP_LOGW(kTag, "Calculation error %d", err);
      return err;
    }
  }

  taskENTER_CRITICAL();
  g_calc.first = result;
  g_calc.second = 0.0;
  g_calc.state = ShowResult;
  int n = snprintf(buffer, sizeof(buffer), "%.8g", result);
  if ((n <= 0) || ((size_t)n >= sizeof(buffer))) {
    ESP_LOGW(kTag, "snprintf error");
    taskEXIT_CRITICAL();
    return ErrFail;
  }
  strncpy((char *)g_calc.display, buffer, sizeof(g_calc.display) - 1U);
  g_calc.display[sizeof(g_calc.display) - 1U] = '\0';
  taskEXIT_CRITICAL();

  err = LcdDisplayLine((const char *)g_calc.display, 0U);
  if (err != ErrOk) {
    ESP_LOGW(kTag, "LCD display line failed");
    return err;
  }
  err = LcdDisplayLine("Result", 1U);
  if (err != ErrOk) {
    ESP_LOGW(kTag, "LCD display line failed");
  }
  return err;
}

/**
 * @brief Handle numeric button input.
 *
 * Appends the digit character to display buffer and updates operand accordingly.
 *
 * @param[in] digit Digit character ('0' to '9').
 *
 * @retval ErrOk On success.
 * @retval ErrFail On unknown calculator state.
 * @retval ErrInvalidArg On string to double conversion failure.
 * @retval ErrLcdFail On LCD display failure.
 */
static ErrorCode HandleNumberButton(char digit) {
  ErrorCode err;
  assert(digit >= '0' && digit <= '9');

  taskENTER_CRITICAL();
  CalcState state = g_calc.state;
  taskEXIT_CRITICAL();

  if ((state == InputFirst) || (state == ShowResult)) {
    if (state == ShowResult) {
      ResetCalc();
    }
    AppendDigit(digit);
    double val = 0.0;
    err = StrToDouble((const char *)g_calc.display, &val);
    if (err != ErrOk) {
      ESP_LOGW(kTag, "str_to_double conversion failed");
      return err;
    }
    taskENTER_CRITICAL();
    g_calc.first = val;
    taskEXIT_CRITICAL();

    err = LcdDisplayLine((const char *)g_calc.display, 0U);
    if (err != ErrOk) {
      ESP_LOGW(kTag, "LCD display line failed");
      return err;
    }
  } else if (state == InputSecond) {
    AppendDigit(digit);
    double val = 0.0;
    err = StrToDouble((const char *)g_calc.display, &val);
    if (err != ErrOk) {
      ESP_LOGW(kTag, "str_to_double conversion failed");
      return err;
    }
    taskENTER_CRITICAL();
    g_calc.second = val;
    taskEXIT_CRITICAL();

    err = LcdDisplayLine((const char *)g_calc.display, 0U);
    if (err != ErrOk) {
      ESP_LOGW(kTag, "LCD display line failed");
      return err;
    }
  } else {
    return ErrFail; /* Unknown state */
  }
  return ErrOk;
}

/**
 * @brief Handle 'Clear' button press.
 *
 * Resets calculator state and clears LCD display.
 *
 * @retval ErrOk On success.
 * @retval ErrLcdFail On LCD display failure.
 */
static ErrorCode HandleClearButton(void) {
  ResetCalc();
  ErrorCode err = LcdDisplayLine((const char *)g_calc.display, 0U);
  if (err != ErrOk) {
    ESP_LOGW(kTag, "LCD display line failed");
    return err;
  }
  err = LcdDisplayLine("", 1U);
  if (err != ErrOk) {
    ESP_LOGW(kTag, "LCD display line failed");
  }
  return err;
}

/**
 * @brief Initialize GPIO pins for buttons as inputs with pull-ups.
 *
 * @retval ErrOk On success.
 * @retval ErrGpioFail On GPIO configuration failure.
 */
static ErrorCode GpioButtonsInit(void) {
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask =
          ((uint64_t)1U << BUTTON_PIN_NUM) | ((uint64_t)1U << BUTTON_PIN_OP) |
          ((uint64_t)1U << BUTTON_PIN_EQ) | ((uint64_t)1U << BUTTON_PIN_CLR),
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .pull_up_en = GPIO_PULLUP_ENABLE,
  };

  int ret = gpio_config(&io_conf);
  if (ret != 0) {
    ESP_LOGE(kTag, "GPIO config failed with code %d", ret);
    return ErrGpioFail;
  }
  return ErrOk;
}

/**
 * @brief Main calculator input processing loop.
 *
 * Continuously polls buttons, handles inputs, and updates calculator state and LCD.
 */
static void CalculatorLoop(void) {
  static volatile int digit = 0;
  char c;
  bool is_pressed;
  ErrorCode err;

  for (;;) {
    err = ButtonPressed((gpio_num_t)BUTTON_PIN_NUM, &is_pressed);
    if (err == ErrOk && is_pressed) {
      c = (char)('0' + digit);
      digit = (digit + 1) % 10;
      err = HandleNumberButton(c);
      if (err != ErrOk) {
        ESP_LOGW(kTag, "handle_number_button failed with error %d", err);
      }
      vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
    }

    err = ButtonPressed((gpio_num_t)BUTTON_PIN_OP, &is_pressed);
    if (err == ErrOk && is_pressed) {
      err = HandleOperatorButton();
      if (err != ErrOk) {
        ESP_LOGW(kTag, "handle_operator_button failed with error %d", err);
      }
      vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
    }

    err = ButtonPressed((gpio_num_t)BUTTON_PIN_EQ, &is_pressed);
    if (err == ErrOk && is_pressed) {
      err = HandleEqualButton();
      if (err != ErrOk) {
        ESP_LOGW(kTag, "handle_equal_button failed with error %d", err);
      }
      vTaskDelay(pdMS_TO_TICKS(BUTTON_REPEAT_DELAY_MS));
    }

    err = ButtonPressed((gpio_num_t)BUTTON_PIN_CLR, &is_pressed);
    if (err == ErrOk && is_pressed) {
      err = HandleClearButton();
      if (err != ErrOk) {
        ESP_LOGW(kTag, "handle_clear_button failed with error %d", err);
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
void app_main(void) {
  ESP_LOGI(kTag, "Calculator Start");
  ErrorCode err;

  err = LcdInit();
  if (err != ErrOk) {
    ESP_LOGE(kTag, "LCD init failed, system halt");
    /* Fail-safe halt */
    for (;;) {
      vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
  }

  err = GpioButtonsInit();
  if (err != ErrOk) {
    ESP_LOGE(kTag, "GPIO init failed, system halt");
    for (;;) {
      vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
  }

  ResetCalc();

  err = LcdDisplayLine((const char *)g_calc.display, 0U);
  if (err != ErrOk) {
    ESP_LOGW(kTag, "LCD display line failed at start");
  }
  err = LcdDisplayLine("", 1U);
  if (err != ErrOk) {
    ESP_LOGW(kTag, "LCD display line failed at start");
  }

  CalculatorLoop();
}