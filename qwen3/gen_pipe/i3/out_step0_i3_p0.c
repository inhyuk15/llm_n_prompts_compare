#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// I2C LCD 설정 (주소: 0x27, 16x2 디스플레이)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// 하드웨어 핀 설정
const int LED_PIN = 2;  // 상품 배출 LED 핀
const int I2C_SDA = 8;  // Xiao-ESP32-S3 I2C SDA 핀
const int I2C_SCL = 9;  // Xiao-ESP32-S3 I2C SCL 핀

// 상품 가격 설정 (상품 1, 2, 3)
const int productPrices[] = {100, 150, 200};

void setup() {
  // 초기화
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // I2C 초기화
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.begin();
  lcd.backlight();
  
  // 시리얼 통신 시작
  Serial.begin(115200);
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    int amount, productNum;
    sscanf(input.c_str(), "%d %d", &amount, &productNum);
    
    // 상품 번호 유효성 검사
    if (productNum < 1 || productNum > 3) {
      lcd.clear();
      lcd.print("Invalid product");
      delay(2000);
      lcd.clear();
      return;
    }
    
    int price = productPrices[productNum - 1];
    
    if (amount >= price) {
      int change = amount - price;
      
      // 상품 배출 LED 켜기
      digitalWrite(LED_PIN, HIGH);
      lcd.clear();
      lcd.print("Dispensing...");
      delay(1000);
      digitalWrite(LED_PIN, LOW);
      
      // 거스름돈 표시
      lcd.clear();
      lcd.print("Change: ");
      lcd.print(change);
    } else {
      lcd.clear();
      lcd.print("Not enough money");
      delay(2000);
      lcd.clear();
    }
  }
}