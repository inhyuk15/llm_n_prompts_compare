#include <LiquidCrystal.h>
#include <Keypad.h>
#include <stdint.h>
#include <string.h>

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
static char operation = ' ';
static bool isSecondNum = false;

void setup() {
  lcd.begin(16, 2);
  lcd.print("Calculator Ready");
  delay(CLEAR_DELAY_MS);
  lcd.clear();
}

void displayInput() {
  lcd.setCursor(0, 0);
  size_t inputLen = strlen(input);
  if (inputLen > LCD_WIDTH) {
    char displayBuff[MAX_DISPLAY_BUFFER];
    const size_t start = inputLen - LCD_WIDTH;
    strncpy(displayBuff, input + start, LCD_WIDTH);
    displayBuff[LCD_WIDTH] = '\0';
    lcd.print(displayBuff);
  } else {
    lcd.print(input);
  }
  lcd.setCursor(0, 1);
  const char* currentNum = isSecondNum ? secondNum : firstNum;
  size_t numLen = strlen(currentNum);
  if (numLen > LCD_WIDTH) {
    char displayBuff[MAX_DISPLAY_BUFFER];
    const size_t start = numLen - LCD_WIDTH;
    strncpy(displayBuff, currentNum + start, LCD_WIDTH);
    displayBuff[LCD_WIDTH] = '\0';
    lcd.print(displayBuff);
  } else {
    lcd.print(currentNum);
  }
}

void loop() {
  char key = keypad.getKey();
  if (key != '\0') {
    if (key == 'C') {
      (void)memset(input, 0, sizeof(input));
      (void)memset(firstNum, 0, sizeof(firstNum));
      (void)memset(secondNum, 0, sizeof(secondNum));
      operation = ' ';
      isSecondNum = false;
      lcd.clear();
    } else if (key == '=') {
      if ((operation != ' ') && (strlen(firstNum) > 0U) && (strlen(secondNum) > 0U)) {
        double num1 = atof(firstNum);
        double num2 = atof(secondNum);
        double result;
        switch (operation) {
          case '+': 
            result = num1 + num2; 
            break;
          case '-': 
            result = num1 - num2; 
            break;
          case '*': 
            result = num1 * num2; 
            break;
          case '/': 
            if (num2 != 0.0) {
              result = num1 / num2;
            } else {
              result = 0.0; // 0으로 나누기 방지
            }
            break;
          default: 
            result = 0.0;
        }
        char formattedResult[INPUT_BUFFER_SIZE];
        (void)snprintf(formattedResult, sizeof(formattedResult), "%s %c %s = %.%ug", 
                      firstNum, operation, secondNum, RESULT_PRECISION, result);
        lcd.clear();
        lcd.setCursor(0, 0);
        if (strlen(formattedResult) > LCD_WIDTH) {
          char temp[MAX_DISPLAY_BUFFER];
          (void)strncpy(temp, formattedResult, LCD_WIDTH);
          temp[LCD_WIDTH] = '\0';
          lcd.print(temp);
        } else {
          lcd.print(formattedResult);
        }
        delay(CLEAR_DELAY_MS);
        lcd.clear();
      }
    } else if ((key == '+') || (key == '-') || (key == '*') || (key == '/')) {
      if (!isSecondNum) {
        (void)snprintf(firstNum, sizeof(firstNum), "%s", input);
        operation = key;
        isSecondNum = true;
        size_t len = strlen(input);
        if (len < (INPUT_BUFFER_SIZE - 1U)) {
          input[len] = key;
          input[len + 1U] = '\0';
        }
      }
    } else {
      if (isSecondNum) {
        size_t len = strlen(secondNum);
        if (len < (NUM_BUFFER_SIZE - 1U)) {
          secondNum[len] = key;
          secondNum[len + 1U] = '\0';
        }
      } else {
        size_t len = strlen(firstNum);
        if (len < (NUM_BUFFER_SIZE - 1U)) {
          firstNum[len] = key;
          firstNum[len + 1U] = '\0';
        }
      }
      size_t len = strlen(input);
      if (len < (INPUT_BUFFER_SIZE - 1U)) {
        input[len] = key;
        input[len + 1U] = '\0';
      }
      displayInput();
    }
  }
}