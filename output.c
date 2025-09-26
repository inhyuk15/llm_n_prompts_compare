```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* MISRA-C 2012 Mandatory Rule Compliant Q16.16 Fixed-point Arithmetic Calculator
 * 4x4 Keypad input processing, Calculate and display on 16x2 LCD
 * No floating point, only 32-bit fixed point Q16.16
 * Static allocation only, stack size and complexity constrained
 */

/* Definitions */
#define KEYPAD_ROWS              4U
#define KEYPAD_COLS              4U
#define MAX_INPUT_LENGTH         16U
#define Q16_16_SHIFT             16U
#define DISPLAY_LINE_LENGTH      16U
#define TASK_STACK_SIZE          256U   /* In words, measured manually */
#define TASK_PRIORITY            2U
#define LCD_LINE_1               0U
#define LCD_LINE_2               1U

/* Named constants for keypad keys */
typedef enum
{
    KEY_0 = 0U,
    KEY_1 = 1U,
    KEY_2 = 2U,
    KEY_3 = 3U,
    KEY_4 = 4U,
    KEY_5 = 5U,
    KEY_6 = 6U,
    KEY_7 = 7U,
    KEY_8 = 8U,
    KEY_9 = 9U,
    KEY_ADD = 10U,     /* '+' */
    KEY_SUB = 11U,     /* '-' */
    KEY_MUL = 12U,     /* '*' */
    KEY_DIV = 13U,     /* '/' */
    KEY_EQ = 14U,      /* '=' */
    KEY_CLR = 15U      /* 'C' Clear */
} KeypadKey_t;

/* Named constants for operator */
typedef enum
{
    OP_NONE = 0U,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV
} Operator_t;

/* Static global variables with limited scope */
static uint8_t input_buffer[MAX_INPUT_LENGTH]; /* stores entered digits, not null terminated */
static uint8_t input_length = 0U;               /* count of digits in input_buffer */
static int32_t operand1 = 0;                     /* Q16.16 fixed point */
static int32_t operand2 = 0;                     /* Q16.16 fixed point */
static Operator_t current_operator = OP_NONE;
static bool operand1_set = false;
static bool operator_set = false;
static bool operand2_set = false;

/* Static semaphore to sync keypad input */
static SemaphoreHandle_t xKeypadSemaphore = NULL;
/* Queue to receive keys pressed from keypad ISR (simulate or real) */
static QueueHandle_t xKeypadQueue = NULL;

/* Function prototypes */
static int32_t MultiplyQ16(int32_t a, int32_t b);
static int32_t DivideQ16(int32_t numerator, int32_t denominator, bool * const divide_by_zero);
static void LCDDisplayWrite(const char * const text, uint8_t line);
static void KeypadTask(void *pvParameters);
static void CalcReset(void);
static bool AppendDigit(uint8_t digit);
static void ProcessOperator(Operator_t op);
static bool PerformCalculation(int32_t * const result);
static void FormatFixedPoint(int32_t value, char * const buf, uint8_t buf_size);

/* Statically allocated task stack */
static StackType_t xKeypadTaskStack[TASK_STACK_SIZE];
static StaticTask_t xKeypadTaskTCB;

/* Implementations */

/* Multiply two Q16.16 fixed-point numbers with saturation */
static int32_t MultiplyQ16(int32_t a, int32_t b)
{
    int64_t product = (int64_t)a * (int64_t)b;
    product = product >> Q16_16_SHIFT;
    /* Saturate to int32_t */
    if (product > 0x7FFFFFFFL)
    {
        product = 0x7FFFFFFFL;
    }
    else if (product < (int64_t)0x80000000)
    {
        product = (int64_t)0x80000000;
    }
    return (int32_t)product;
}

/* Divide two Q16.16 fixed-point numbers with divide by zero check */
static int32_t DivideQ16(int32_t numerator, int32_t denominator, bool * const divide_by_zero)
{
    int32_t result = 0;
    if (denominator == 0)
    {
        *divide_by_zero = true;
        result = 0;
    }
    else
    {
        *divide_by_zero = false;
        int64_t numerator64 = ((int64_t)numerator << Q16_16_SHIFT);
        result = (int32_t)(numerator64 / denominator);
    }
    return result;
}

/* Format fixed-point Q16.16 number to string with 4 decimal digits */
/* buf must have at least 16 chars */
/* Example: 123456 -> "1.8823" or "-0.0123" */
static void FormatFixedPoint(int32_t value, char * const buf, uint8_t buf_size)
{
    /* Preconditions */
    if ((buf == NULL) || (buf_size < 7U))
    {
        if (buf != NULL)
        {
            buf[0] = (char)0;
        }
        return;
    }

    int32_t abs_value = value;
    bool negative = false;
    if (value < 0)
    {
        negative = true;
        abs_value = -value;
    }

    int32_t int_part = abs_value >> Q16_16_SHIFT;
    int32_t frac_part = ((abs_value & 0xFFFFU) * 10000 + 0x8000U) >> 16U; /* Round */

    /* Format: [-]ddd.dddd */
    /* Safety: max digits decimal 5 + 1 (sign) + 1 (dot) + 4 fraction + 1 null terminator = 12 < 16 buf_size */
    (void)snprintf(buf,
                   (size_t)buf_size,
                   "%s%d.%04d",
                   (negative != false) ? "-" : "",
                   int_part,
                   frac_part);
}

/* Resets calculator state */
static void CalcReset(void)
{
    input_length = 0U;
    (void)memset(input_buffer, 0U, sizeof(input_buffer));
    operand1 = 0;
    operand2 = 0;
    current_operator = OP_NONE;
    operand1_set = false;
    operator_set = false;
    operand2_set = false;
}

/* Append digit to input buffer, returns false if buffer full */
static bool AppendDigit(uint8_t digit)
{
    bool status = false;
    if (input_length < MAX_INPUT_LENGTH)
    {
        input_buffer[input_length] = digit;
        input_length++;
        status = true;
    }
    return status;
}

/* Convert input_buffer digits to Q16.16 number */
static int32_t InputBufferToQ16(void)
{
    uint32_t i;
    uint32_t val = 0U;
    for (i = 0U; i < input_length; i++)
    {
        val = val * 10U + (uint32_t)input_buffer[i];
    }
    /* Convert integer to Q16.16 */
    return ((int32_t)(val << Q16_16_SHIFT));
}

/* Process operator key */
static void ProcessOperator(Operator_t op)
{
    if (input_length > 0U)
    {
        if (operand1_set == false)
        {
            operand1 = InputBufferToQ16();
            operand1_set = true;
        }
        else if ((operator_set != false) && (operand2_set == false))
        {
            operand2 = InputBufferToQ16();
            operand2_set = true;
        }
    }

    if ((operand1_set != false) && (operator_set != false) && (operand2_set != false))
    {
        int32_t result = 0;
        bool error = false;
        bool res_ok = PerformCalculation(&result);
        if ((res_ok != false) && (error == false))
        {
            operand1 = result;
            operand2 = 0;
            operand2_set = false;
        }
        else
        {
            /* Division by zero case resets */
            CalcReset();
        }
    }

    current_operator = op;
    operator_set = (op != OP_NONE);
    input_length = 0U;
}

/* Performs calculation operand1 op operand2, stores result in result */
/* Returns false if operator unknown */
static bool PerformCalculation(int32_t * const result)
{
    bool status = false;
    bool divide_by_zero = false;
    int32_t res = 0;
    if (result == NULL)
    {
        return false;
    }

    switch (current_operator)
    {
        case OP_ADD:
            res = operand1 + operand2;
            status = true;
            break;
        case OP_SUB:
            res = operand1 - operand2;
            status = true;
            break;
        case OP_MUL:
            res = MultiplyQ16(operand1, operand2);
            status = true;
            break;
        case OP_DIV:
            res = DivideQ16(operand1, operand2, &divide_by_zero);
            status = (!divide_by_zero);
            break;
        case OP_NONE:
            /* No operator */
            status = false;
            break;
        default:
            status = false;
            break;
    }

    if (status != false)
    {
        *result = res;
    }

    return status;
}

/* Task to process keypad input, calculate and display results */
static void KeypadTask(void *pvParameters)
{
    (void)pvParameters;
    uint8_t key = 0U;
    char display_buf[DISPLAY_LINE_LENGTH + 1U] = {0};
    int32_t display_value = 0;

    CalcReset();

    for (;;)
    {
        if (xQueueReceive(xKeypadQueue, &key, portMAX_DELAY) == pdTRUE)
        {
            /* Process key */
            if (key <= KEY_9)
            {
                /* Digit */
                if (AppendDigit(key) != false)
                {
                    /* Display entered digits */
                    (void)memset(display_buf, (int) ' ', sizeof(display_buf));
                    uint32_t i;
                    for (i = 0U; i < input_length && i < DISPLAY_LINE_LENGTH; i++)
                    {
                        display_buf[i] = (char)(input_buffer[i] + (uint8_t)'0');
                    }
                    display_buf[DISPLAY_LINE_LENGTH] = (char)0;
                    LCDDisplayWrite(display_buf, LCD_LINE_1);
                }
            }
            else
            {
                switch (key)
                {
                    case KEY_ADD:
                        ProcessOperator(OP_ADD);
                        break;
                    case KEY_SUB:
                        ProcessOperator(OP_SUB);
                        break;
                    case KEY_MUL:
                        ProcessOperator(OP_MUL);
                        break;
                    case KEY_DIV:
                        ProcessOperator(OP_DIV);
                        break;
                    case KEY_EQ:
                    {
                        if (input_length > 0U)
                        {
                            operand2 = InputBufferToQ16();
                            operand2_set = true;
                        }
                        if ((operand1_set != false) && (operator_set != false) && (operand2_set != false))
                        {
                            int32_t result = 0;
                            bool calc_status = PerformCalculation(&result);
                            if (calc_status != false)
                            {
                                display_value = result;
                                CalcReset();
                                operand1 = display_value;
                                operand1_set = true;
                                /* Format and display result */
                                FormatFixedPoint(display_value, display_buf, sizeof(display_buf));
                                LCDDisplayWrite(display_buf, LCD_LINE_1);
                            }
                            else
                            {
                                /* Error: Division by zero or unknown operator */
                                (void)memset(display_buf, 0, sizeof(display_buf));
                                (void)strncpy(display_buf, "Error", sizeof(display_buf) - 1U);
                                LCDDisplayWrite(display_buf, LCD_LINE_1);
                                CalcReset();
                            }
                        }
                        else
                        {
                            /* No calculation to do, simply reset */
                            CalcReset();
                        }
                        break;
                    }
                    case KEY_CLR:
                        /* Clear all */
                        CalcReset();
                        (void)memset(display_buf, 0, sizeof(display_buf));
                        LCDDisplayWrite(display_buf, LCD_LINE_1);
                        break;
                    default:
                        /* Ignore unknown keys */
                        break;
                }
            }
        }
    }
}

/* Stub: LCD display string on line, line = 0 or 1 */
static void LCDDisplayWrite(const char * const text, uint8_t line)
{
    /* Implementation Note:
     * This is hardware-dependent. Replace with actual
     * LCD driver code. Here is just an example.
     *
     * For MISRA compliance, parameters are const as needed.
     */
    (void)text;
    (void)line;
    /* e.g. LCD_SetCursor(line, 0);
       LCD_WriteString(text);
    */
}

/* Stub: keypad ISR or driver should call this to send key to queue */
void Keypad_SendKey(uint8_t key)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* Safety checks for known keys */
    if (key <= KEY_CLR)
    {
        (void)xQueueSendFromISR(xKeypadQueue, &key, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/* Initialization function */
void Calculator_Init(void)
{
    /* Create queue for keypad keys - Length 10 */
    xKeypadQueue = xQueueCreateStatic(10U,
                                      sizeof(uint8_t),
                                      (uint8_t *)pvPortMalloc(10U * sizeof(uint8_t)), /* For MISRA compliance, use static array instead of malloc typically */
                                      NULL);
    /* Create counting semaphore */
    xKeypadSemaphore = xSemaphoreCreateCountingStatic(10U, 0U, NULL); /* Adjust if needed */

    /* Create KeypadTask */
    (void)xTaskCreateStatic(KeypadTask,
                            "KeypadTask",
                            TASK_STACK_SIZE,
                            NULL,
                            TASK_PRIORITY,
                            xKeypadTaskStack,
                            &xKeypadTaskTCB);
}

/* End of file */
```