#include <LiquidCrystal.h>
#include <Keypad.h>
#include <stdint.h>
#include <string.h>

// Error codes
typedef enum {
    ERROR_NONE,
    ERROR_NULL_POINTER,
    ERROR_BUFFER_OVERFLOW,
    ERROR_INVALID_OPERATION,
    ERROR_DIVISION_BY_ZERO,
    ERROR_LCD_FAILURE,
    ERROR_KEYBOARD_FAILURE
} ErrorCode;

// LCD 핀 설정
LiquidCrystal lcd(21, 22, 23, 18, 19, 17);

// 키패드 설정
const uint8_t ROWS = 4;
const uint8_t COLS = 4;
char keys[ROWS][COLS] = {
  {'7','8','9','/'},
  {'4','5','6','*'},
  {'1','2','3','-'},
  {'C','0','=','+'}
};
uint8_t rowPins[ROWS] = {12, 13, 14, 27};
uint8_t colPins[COLS] = {26, 33, 32, 35};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// 고정 크기 버퍼 선언
const size_t INPUT_BUFFER_SIZE = 32U;
const size_t NUM_BUFFER_SIZE = 16U;
const size_t LCD_WIDTH = 16U;
const size_t MAX_DISPLAY_BUFFER = LCD_WIDTH + 1U;
const uint16_t CLEAR_DELAY_MS = 2000U;
const uint8_t RESULT_PRECISION = 10U;

static char input[INPUT_BUFFER_SIZE] = "";
static char firstNum[NUM_BUFFER_SIZE] = "";
static char secondNum[NUM_BUFFER_SIZE] = "";
static volatile char operation = ' ';
static volatile bool isSecondNum = false;

ErrorCode truncateToLCD(char* dest, const char* src, size_t maxLength) {
    if (dest == NULL || src == NULL || maxLength == 0U) {
        return ERROR_NULL_POINTER;
    }
    
    size_t srcLen = strlen(src);
    if (srcLen > maxLength) {
        strncpy(dest, src + (srcLen - maxLength), maxLength);
    } else {
        strncpy(dest, src, maxLength);
    }
    dest[maxLength] = '\0';
    return ERROR_NONE;
}

ErrorCode displayInput() {
    noInterrupts();
    lcd.setCursor(0, 0);
    char line1[MAX_DISPLAY_BUFFER] = {0};
    size_t inputLen = strlen(input);
    if (inputLen > LCD_WIDTH) {
        ErrorCode err = truncateToLCD(line1, input, LCD_WIDTH);
        if (err != ERROR_NONE) {
            interrupts();
            return err;
        }
    } else {
        strncpy(line1, input, LCD_WIDTH);
        line1[LCD_WIDTH] = '\0';
    }
    lcd.print(line1);

    lcd.setCursor(0, 1);
    const char* currentNum = isSecondNum ? secondNum : firstNum;
    char line2[MAX_DISPLAY_BUFFER] = {0};
    size_t numLen = strlen(currentNum);
    if (numLen > LCD_WIDTH) {
        ErrorCode err = truncateToLCD(line2, currentNum, LCD_WIDTH);
        if (err != ERROR_NONE) {
            interrupts();
            return err;
        }
    } else {
        strncpy(line2, currentNum, LCD_WIDTH);
        line2[LCD_WIDTH] = '\0';
    }
    lcd.print(line2);
    interrupts();
    return ERROR_NONE;
}

ErrorCode appendChar(char* buffer, char ch, size_t bufferSize) {
    if (buffer == NULL || bufferSize == 0) {
        return ERROR_NULL_POINTER;
    }
    size_t len = strlen(buffer);
    if (len < (bufferSize - 1U)) {
        buffer[len] = ch;
        buffer[len + 1U] = '\0';
        return ERROR_NONE;
    } else {
        return ERROR_BUFFER_OVERFLOW;
    }
}

bool isOperation(char key) {
    return (key == '+') || (key == '-') || (key == '*') || (key == '/');
}

ErrorCode handleClear() {
    noInterrupts();
    (void)memset(input, 0, sizeof(input));
    (void)memset(firstNum, 0, sizeof(firstNum));
    (void)memset(secondNum, 0, sizeof(secondNum));
    operation = ' ';
    isSecondNum = false;
    interrupts();
    return ERROR_NONE;
}

double calculateResult(double num1, double num2, char op, ErrorCode* err) {
    if (op != '+' && op != '-' && op != '*' && op != '/') {
        if (err != NULL) {
            *err = ERROR_INVALID_OPERATION;
        }
        return 0.0;
    }
    double result = 0.0;
    switch (op) {
        case '+': result = num1 + num2; break;
        case '-': result = num1 - num2; break;
        case '*': result = num1 * num2; break;
        case '/': 
            if (num2 != 0.0) {
                result = num1 / num2;
            } else {
                if (err != NULL) {
                    *err = ERROR_DIVISION_BY_ZERO;
                }
                return 0.0;
            }
            break;
    }
    if (err != NULL) {
        *err = ERROR_NONE;
    }
    return result;
}

ErrorCode displayResult(double result) {
    char localFirstNum[NUM_BUFFER_SIZE];
    char localSecondNum[NUM_BUFFER_SIZE];
    char localOperation;

    noInterrupts();
    strncpy(localFirstNum, firstNum, sizeof(localFirstNum));
    localFirstNum[sizeof(localFirstNum)-1] = '\0';
    strncpy(localSecondNum, secondNum, sizeof(localSecondNum));
    localSecondNum[sizeof(localSecondNum)-1] = '\0';
    localOperation = operation;
    interrupts();

    char formattedResult[INPUT_BUFFER_SIZE] = {0};
    int len = snprintf(formattedResult, sizeof(formattedResult), "%s %c %s = %.%g", 
                localFirstNum, localOperation, localSecondNum, RESULT_PRECISION, result);
    if (len < 0 || len >= (int)sizeof(formattedResult)) {
        return ERROR_BUFFER_OVERFLOW;
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    char line[MAX_DISPLAY_BUFFER] = {0};
    size_t lenFormatted = strlen(formattedResult);
    if (lenFormatted > LCD_WIDTH) {
        ErrorCode err = truncateToLCD(line, formattedResult, LCD_WIDTH);
        if (err != ERROR_NONE) {
            return err;
        }
    } else {
        strncpy(line, formattedResult, LCD_WIDTH);
        line[LCD_WIDTH] = '\0';
    }
    lcd.print(line);
    delay(CLEAR_DELAY_MS);
    lcd.clear();
    return ERROR_NONE;
}

ErrorCode handleEquals() {
    noInterrupts();
    if ((operation != ' ') && (strlen(firstNum) > 0U) && (strlen(secondNum) > 0U)) {
        double num1 = atof(firstNum);
        double num2 = atof(secondNum);
        ErrorCode err;
        double result = calculateResult(num1, num2, operation, &err);
        if (err != ERROR_NONE) {
            interrupts();
            return err;
        }
        interrupts();
        ErrorCode dispErr = displayResult(result);
        if (dispErr != ERROR_NONE) {
            return dispErr;
        }
    } else {
        interrupts();
        return ERROR_INVALID_OPERATION;
    }
    interrupts();
    return ERROR_NONE;
}

ErrorCode handleOperation(char key) {
    if (key != '+' && key != '-' && key != '*' && key != '/') {
        return ERROR_INVALID_OPERATION;
    }
    noInterrupts();
    if (!isSecondNum) {
        int len = snprintf(firstNum, sizeof(firstNum), "%s", input);
        if (len < 0 || len >= (int)sizeof(firstNum)) {
            interrupts();
            return ERROR_BUFFER_OVERFLOW;
        }
        operation = key;
        isSecondNum = true;
        ErrorCode err = appendChar(input, key, INPUT_BUFFER_SIZE);
        if (err != ERROR_NONE) {
            interrupts();
            return err;
        }
    }
    interrupts();
    return ERROR_NONE;
}

ErrorCode handleDigit(char key) {
    if (!((key >= '0' && key <= '9') || key == '.')) {
        return ERROR_INVALID_OPERATION;
    }
    noInterrupts();
    if (isSecondNum) {
        ErrorCode err = appendChar(secondNum, key, NUM_BUFFER_SIZE);
        if (err != ERROR_NONE) {
            interrupts();
            return err;
        }
    } else {
        ErrorCode err = appendChar(firstNum, key, NUM_BUFFER_SIZE);
        if (err != ERROR_NONE) {
            interrupts();
            return err;
        }
    }
    ErrorCode err = appendChar(input, key, INPUT_BUFFER_SIZE);
    if (err != ERROR_NONE) {
        interrupts();
        return err;
    }
    ErrorCode dispErr = displayInput();
    if (dispErr != ERROR_NONE) {
        interrupts();
        return dispErr;
    }
    interrupts();
    return ERROR_NONE;
}

void setup() {
    lcd.begin(16, 2);
    lcd.print("Calculator Ready");
    delay(CLEAR_DELAY_MS);
    lcd.clear();
}

void loop() {
    char key = keypad.getKey();
    if (key != '\0') {
        if (key == 'C') {
            ErrorCode err = handleClear();
            if (err != ERROR_NONE) {
                char errorMsg[INPUT_BUFFER_SIZE];
                snprintf(errorMsg, sizeof(errorMsg), "Error %d", err);
                lcd.clear();
                lcd.print(errorMsg);
                delay(CLEAR_DELAY_MS);
                lcd.clear();
            }
        } else if (key == '=') {
            ErrorCode err = handleEquals();
            if (err != ERROR_NONE) {
                char errorMsg[INPUT_BUFFER_SIZE];
                snprintf(errorMsg, sizeof(errorMsg), "Error %d", err);
                lcd.clear();
                lcd.print(errorMsg);
                delay(CLEAR_DELAY_MS);
                lcd.clear();
            }
        } else if (isOperation(key)) {
            ErrorCode err = handleOperation(key);
            if (err != ERROR_NONE) {
                char errorMsg[INPUT_BUFFER_SIZE];
                snprintf(errorMsg, sizeof(errorMsg), "Error %d", err);
                lcd.clear();
                lcd.print(errorMsg);
                delay(CLEAR_DELAY_MS);
                lcd.clear();
            }
        } else {
            ErrorCode err = handleDigit(key);
            if (err != ERROR_NONE) {
                char errorMsg[INPUT_BUFFER_SIZE];
                snprintf(errorMsg, sizeof(errorMsg), "Error %d", err);
                lcd.clear();
                lcd.print(errorMsg);
                delay(CLEAR_DELAY_MS);
                lcd.clear();
            }
        }
    }
}