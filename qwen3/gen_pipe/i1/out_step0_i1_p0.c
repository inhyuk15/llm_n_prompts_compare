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

// 변수 선언
String input = "";
String firstNum = "";
String secondNum = "";
char operation = ' ';
bool isSecondNum = false;

void setup() {
  lcd.begin(16, 2);
  lcd.print("Calculator Ready");
  delay(2000);
  lcd.clear();
}

void displayInput() {
  lcd.setCursor(0, 0);
  if (input.length() > 16) {
    lcd.print(input.substring(input.length() - 16));
  } else {
    lcd.print(input);
  }
  lcd.setCursor(0, 1);
  if (isSecondNum) {
    if (secondNum.length() > 16) {
      lcd.print(secondNum.substring(secondNum.length() - 16));
    } else {
      lcd.print(secondNum);
    }
  } else {
    if (firstNum.length() > 16) {
      lcd.print(firstNum.substring(firstNum.length() - 16));
    } else {
      lcd.print(firstNum);
    }
  }
}

void loop() {
  char key = keypad.getKey();
  if (key) {
    if (key == 'C') {
      input = "";
      firstNum = "";
      secondNum = "";
      operation = ' ';
      isSecondNum = false;
      lcd.clear();
    } else if (key == '=') {
      if (operation != ' ' && firstNum != "" && secondNum != "") {
        double num1 = firstNum.toDouble();
        double num2 = secondNum.toDouble();
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
        input = String(num1) + " " + operation + " " + num2 + " = " + result;
        lcd.clear();
        lcd.setCursor(0, 0);
        if (input.length() > 16) {
          lcd.print(input.substring(0,16));
        } else {
          lcd.print(input);
        }
        delay(2000);
        lcd.clear();
      }
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
      if (!isSecondNum) {
        firstNum = input;
        operation = key;
        isSecondNum = true;
        input += key;
      }
    } else {
      if (isSecondNum) {
        secondNum += key;
      } else {
        firstNum += key;
      }
      input += key;
      displayInput();
    }
  }
}