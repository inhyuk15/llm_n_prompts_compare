#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Stack usage: 0 bytes (no local variables)
static LiquidCrystal_I2C lcd(0x27, 16, 2); // I2C LCD (Address: 0x27, 16x2 Display)
static const int LED_PIN = 2;  // Product dispensing LED pin
static const int I2C_SDA = 8;  // Xiao-ESP32-S3 I2C SDA pin
static const int I2C_SCL = 9;  // Xiao-ESP32-S3 I2C SCL pin
static const int productPrices[] = {100, 150, 200}; // Product prices (Product 1, 2, 3)

// Stack usage: 0 bytes (no local variables)
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.begin();
  lcd.backlight();
  Serial.begin(115200);
}

// Stack usage: 52 bytes (32-byte input buffer + 4 local ints)
void loop() {
  if (Serial.available()) {
    char inputBuffer[32] = {0}; // 32-byte input buffer
    int bytesRead = Serial.readBytesUntil('\n', inputBuffer, sizeof(inputBuffer) - 1);
    inputBuffer[bytesRead] = '\0'; // Null-terminate
    
    // Manual trim (leading/trailing whitespace)
    char *start = inputBuffer;
    while (*start == ' ') start++;
    
    char *end = start;
    while (*end != '\0') end++;
    end--;
    while (end >= start && *end == ' ') end--;
    *(end + 1) = '\0';
    
    int amount = 0, productNum = 0;
    int parsed = sscanf(start, "%d %d", &amount, &productNum);
    
    if (parsed != 2) {
      lcd.clear();
      lcd.print("Invalid format");
      delay(2000);
      lcd.clear();
      return;
    }
    
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
      
      digitalWrite(LED_PIN, HIGH);
      lcd.clear();
      lcd.print("Dispensing...");
      delay(1000);
      digitalWrite(LED_PIN, LOW);
      
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