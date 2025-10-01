#include <LiquidCrystal.h>
#include <Keypad.h>

// LCD 핀 설정
LiquidCrystal lcd(21, 22, 23, 18, 19, 17);

// 키패드 설정
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'7','8','9','/'},
  {'4','5','6','*'},
  {'1','2','3','-'},
  {'C','0','=','+'}
};
byte rowPins[ROWS] = {12, 13, 14, 27};
byte colPins[COLS] = {26, 33, 32, 35};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// 고정 크기 버퍼 선언
#define INPUT_BUFFER_SIZE 32
#define NUM_BUFFER_SIZE 16

static char input[INPUT_BUFFER_SIZE] = "";
static char firstNum[NUM_BUFFER_SIZE] = "";
static char secondNum[NUM_BUFFER_SIZE] = "";
static char operation = ' ';
static bool isSecondNum = false;

void setup() {
  lcd.begin(16, 2);
  lcd.print("Calculator Ready");
  delay(2000);
  lcd.clear();
}

// 스택 사용량: ~17바이트 (displayBuff)
void displayInput() {
  lcd.setCursor(0, 0);
  size_t inputLen = strlen(input);
  if (inputLen > 16) {
    char displayBuff[17];
    int start = inputLen - 16;
    strncpy(displayBuff, input + start, 16);
    displayBuff[16] = '\0';
    lcd.print(displayBuff);
  } else {
    lcd.print(input);
  }
  lcd.setCursor(0, 1);
  const char* currentNum = isSecondNum ? secondNum : firstNum;
  size_t numLen = strlen(currentNum);
  if (numLen > 16) {
    char displayBuff[17];
    int start = numLen - 16;
    strncpy(displayBuff, currentNum + start, 16);
    displayBuff[16] = '\0';
    lcd.print(displayBuff);
  } else {
    lcd.print(currentNum);
  }
}

void loop() {
  char key = keypad.getKey();
  if (key) {
    if (key == 'C') {
      memset(input, 0, sizeof(input));
      memset(firstNum, 0, sizeof(firstNum));
      memset(secondNum, 0, sizeof(secondNum));
      operation = ' ';
      isSecondNum = false;
      lcd.clear();
    } else if (key == '=') {
      if (operation != ' ' && strlen(firstNum) > 0 && strlen(secondNum) > 0) {
        double num1 = atof(firstNum);
        double num2 = atof(secondNum);
        double result;
        switch (operation) {
          case '+': result = num1 + num2; break;
          case '-': result = num1 - num2; break;
          case '*': result = num1 * num2; break;
          case '/': 
            if (num2 != 0) result = num1 / num2;
            else result = 0; // 0으로 나누기 방지
            break;
          default: result = 0;
        }
        // 결과 문자열 포맷 (버퍼 오버플로우 방지)
        snprintf(input, sizeof(input), "%s %c %s = %.10g", firstNum, operation, secondNum, result);
        lcd.clear();
        lcd.setCursor(0, 0);
        if (strlen(input) > 16) {
          char temp[17];
          strncpy(temp, input, 16);
          temp[16] = '\0';
          lcd.print(temp);
        } else {
          lcd.print(input);
        }
        delay(2000);
        lcd.clear();
      }
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
      if (!isSecondNum) {
        // 첫 번째 숫자 저장
        snprintf(firstNum, sizeof(firstNum), "%s", input);
        operation = key;
        isSecondNum = true;
        // 연산자 추가 (버퍼 경계 체크)
        size_t len = strlen(input);
        if (len < INPUT_BUFFER_SIZE - 1) {
          input[len] = key;
          input[len + 1] = '\0';
        }
      }
    } else {
      // 숫자 추가 (버퍼 경계 체크)
      if (isSecondNum) {
        size_t len = strlen(secondNum);
        if (len < NUM_BUFFER_SIZE - 1) {
          secondNum[len] = key;
          secondNum[len + 1] = '\0';
        }
      } else {
        size_t len = strlen(firstNum);
        if (len < NUM_BUFFER_SIZE - 1) {
          firstNum[len] = key;
          firstNum[len + 1] = '\0';
        }
      }
      // 입력 버퍼 업데이트
      size_t len = strlen(input);
      if (len < INPUT_BUFFER_SIZE - 1) {
        input[len] = key;
        input[len + 1] = '\0';
      }
      displayInput();
    }
  }
}