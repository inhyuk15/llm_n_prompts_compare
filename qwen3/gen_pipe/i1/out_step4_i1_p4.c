#include <LiquidCrystal.h>
#include <Keypad.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

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

void truncateToLCD(char* dest, const char* src, size_t maxLength) {
  assert(dest != NULL);
  assert(src != NULL);
  assert(maxLength > 0U);
  
  size_t srcLen = strlen(src);
  if (srcLen > maxLength) {
    // srcLen >= maxLength 인 경우에만 substr 사용
    strncpy(dest, src + (srcLen - maxLength), maxLength);
  } else {
    strncpy(dest, src, maxLength);
  }
  dest[maxLength] = '\0';
}

void displayInput() {
  lcd.setCursor(0, 0);
  char line1[MAX_DISPLAY_BUFFER] = {0};
  size_t inputLen = strlen(input);
  if (inputLen > LCD_WIDTH) {
    truncateToLCD(line1, input, LCD_WIDTH);
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
    truncateToLCD(line2, currentNum, LCD_WIDTH);
  } else {
    strncpy(line2, currentNum, LCD_WIDTH);
    line2[LCD_WIDTH] = '\0';
  }
  lcd.print(line2);
}

void appendChar(char* buffer, char ch, size_t bufferSize) {
  assert(buffer != NULL);
  assert(bufferSize > 0U);
  
  size_t len = strlen(buffer);
  if (len < (bufferSize - 1U)) {
    buffer[len] = ch;
    buffer[len + 1U] = '\0';
  }
}

bool isOperation(char key) {
  return (key == '+') || (key == '-') || (key == '*') || (key == '/');
}

void handleClear() {
  (void)memset(input, 0, sizeof(input));
  (void)memset(firstNum, 0, sizeof(firstNum));
  (void)memset(secondNum, 0, sizeof(secondNum));
  operation = ' ';
  isSecondNum = false;
  lcd.clear();
}

double calculateResult(double num1, double num2, char op) {
  assert(op == '+' || op == '-' || op == '*' || op == '/');
  double result = 0.0;
  switch (op) {
    case '+': result = num1 + num2; break;
    case '-': result = num1 - num2; break;
    case '*': result = num1 * num2; break;
    case '/': 
      if (num2 != 0.0) {
        result = num1 / num2;
      }
      break;
    default: break;
  }
  return result;
}

void displayResult(double result) {
  char formattedResult[INPUT_BUFFER_SIZE] = {0};
  (void)snprintf(formattedResult, sizeof(formattedResult), "%s %c %s = %.%g", 
                firstNum, operation, secondNum, RESULT_PRECISION, result);
  lcd.clear();
  lcd.setCursor(0, 0);
  char line[MAX_DISPLAY_BUFFER] = {0};
  if (strlen(formattedResult) > LCD_WIDTH) {
    truncateToLCD(line, formattedResult, LCD_WIDTH);
  } else {
    strncpy(line, formattedResult, LCD_WIDTH);
    line[LCD_WIDTH] = '\0';
  }
  lcd.print(line);
  delay(CLEAR_DELAY_MS);
  lcd.clear();
}

void handleEquals() {
  if ((operation != ' ') && (strlen(firstNum) > 0U) && (strlen(secondNum) > 0U)) {
    double num1 = atof(firstNum);
    double num2 = atof(secondNum);
    double result = calculateResult(num1, num2, operation);
    displayResult(result);
  }
}

void handleOperation(char key) {
  assert(key == '+' || key == '-' || key == '*' || key == '/');
  if (!isSecondNum) {
    (void)snprintf(firstNum, sizeof(firstNum), "%s", input);
    operation = key;
    isSecondNum = true;
    appendChar(input, key, INPUT_BUFFER_SIZE);
  }
}

void handleDigit(char key) {
  assert((key >= '0' && key <= '9') || key == '.');
  if (isSecondNum) {
    appendChar(secondNum, key, NUM_BUFFER_SIZE);
  } else {
    appendChar(firstNum, key, NUM_BUFFER_SIZE);
  }
  appendChar(input, key, INPUT_BUFFER_SIZE);
  displayInput();
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
      handleClear();
    } else if (key == '=') {
      handleEquals();
    } else if (isOperation(key)) {
      handleOperation(key);
    } else {
      handleDigit(key);
    }
  }
}