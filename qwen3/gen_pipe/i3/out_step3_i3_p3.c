#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Stack usage: 0 bytes (no local variables)
static const int LCD_ADDRESS = 0x27; // I2C LCD Address
static const int LCD_COLS = 16;      // LCD Columns
static const int LCD_ROWS = 2;       // LCD Rows
static const int LED_PIN = 2;        // Product dispensing LED pin
static const int I2C_SDA = 8;        // Xiao-ESP32-S3 I2C SDA pin
static const int I2C_SCL = 9;        // Xiao-ESP32-S3 I2C SCL pin
static const int PRODUCT_PRICE_1 = 100;
static const int PRODUCT_PRICE_2 = 150;
static const int PRODUCT_PRICE_3 = 200;
static const int INPUT_BUFFER_SIZE = 32; // Input buffer size
static const int BAUD_RATE = 115200;     // Serial baud rate
static const int DELAY_SHORT = 1000;     // 1 second delay
static const int DELAY_LONG = 2000;      // 2 seconds delay
static const int PRODUCT_COUNT = 3;      // Number of products
static const int productPrices[] = {PRODUCT_PRICE_1, PRODUCT_PRICE_2, PRODUCT_PRICE_3}; // Product prices

static LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);

// Function declarations
static char* trimInput(char* input);
static bool parseInput(const char* input, int* amount, int* productNum);
static bool validateProduct(int productNum);
static void displayInvalidFormat();
static void displayInvalidProduct();
static void dispenseProduct(int amount, int productNum);

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.begin();
  lcd.backlight();
  Serial.begin(BAUD_RATE);
}

void loop() {
  if (Serial.available()) {
    char inputBuffer[INPUT_BUFFER_SIZE] = {0}; // 32-byte input buffer
    int bytesRead = Serial.readBytesUntil('\n', inputBuffer, sizeof(inputBuffer) - 1);
    inputBuffer[bytesRead] = '\0'; // Null-terminate
    
    char* trimmedInput = trimInput(inputBuffer);
    
    int amount = 0, productNum = 0;
    bool parsed = parseInput(trimmedInput, &amount, &productNum);
    
    if (!parsed) {
      displayInvalidFormat();
      return;
    }
    
    if (!validateProduct(productNum)) {
      displayInvalidProduct();
      return;
    }
    
    dispenseProduct(amount, productNum);
  }
}

static char* trimInput(char* input) {
  char* start = input;
  while (*start == ' ') start++;
  
  char* end = start;
  while (*end != '\0') end++;
  end--;
  while (end >= start && *end == ' ') end--;
  *(end + 1) = '\0';
  
  return start;
}

static bool parseInput(const char* input, int* amount, int* productNum) {
  int parsed = sscanf(input, "%d %d", amount, productNum);
  return parsed == 2;
}

static bool validateProduct(int productNum) {
  return productNum >= 1 && productNum <= PRODUCT_COUNT;
}

static void displayInvalidFormat() {
  lcd.clear();
  lcd.print("Invalid format");
  delay(DELAY_LONG);
  lcd.clear();
}

static void displayInvalidProduct() {
  lcd.clear();
  lcd.print("Invalid product");
  delay(DELAY_LONG);
  lcd.clear();
}

static void dispenseProduct(int amount, int productNum) {
  int price = productPrices[productNum - 1];
  
  if (amount >= price) {
    int change = amount - price;
    
    digitalWrite(LED_PIN, HIGH);
    lcd.clear();
    lcd.print("Dispensing...");
    delay(DELAY_SHORT);
    digitalWrite(LED_PIN, LOW);
    
    lcd.clear();
    lcd.print("Change: ");
    lcd.print(change);
  } else {
    lcd.clear();
    lcd.print("Not enough money");
    delay(DELAY_LONG);
    lcd.clear();
  }
}