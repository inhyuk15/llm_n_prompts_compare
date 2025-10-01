#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <assert.h>

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

// Mutex for critical section protection
static volatile uint8_t lcd_mutex = 0;
static volatile uint8_t led_mutex = 0;

// Function declarations
static char* trimInput(char* input);
static bool parseInput(const char* input, int* amount, int* productNum);
static bool validateProduct(int productNum);
static void displayInvalidFormat();
static void displayInvalidProduct();
static void dispenseProduct(int amount, int productNum);

// Critical section protection functions
static void acquire_lcd() {
  noInterrupts();
  while (lcd_mutex) {} // Wait for release
  lcd_mutex = 1;
  interrupts();
}

static void release_lcd() {
  noInterrupts();
  lcd_mutex = 0;
  interrupts();
}

static void acquire_led() {
  noInterrupts();
  while (led_mutex) {} // Wait for release
  led_mutex = 1;
  interrupts();
}

static void release_led() {
  noInterrupts();
  led_mutex = 0;
  interrupts();
}

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
    
    if (bytesRead == 0) {
      displayInvalidFormat();
      return;
    }
    
    char* trimmedInput = trimInput(inputBuffer);
    if (trimmedInput == NULL) {
      displayInvalidFormat();
      return;
    }
    
    int amount = 0, productNum = 0;
    bool parsed = parseInput(trimmedInput, &amount, &productNum);
    
    if (!parsed) {
      displayInvalidFormat();
      return;
    }
    
    if (amount < 0) {
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
  assert(input != NULL);
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
  assert(input != NULL);
  assert(amount != NULL);
  assert(productNum != NULL);
  int parsed = sscanf(input, "%d %d", amount, productNum);
  return parsed == 2;
}

static bool validateProduct(int productNum) {
  return productNum >= 1 && productNum <= PRODUCT_COUNT;
}

static void displayInvalidFormat() {
  acquire_lcd();
  lcd.clear();
  lcd.print("Invalid format");
  release_lcd();
  delay(DELAY_LONG);
  acquire_lcd();
  lcd.clear();
  release_lcd();
}

static void displayInvalidProduct() {
  acquire_lcd();
  lcd.clear();
  lcd.print("Invalid product");
  release_lcd();
  delay(DELAY_LONG);
  acquire_lcd();
  lcd.clear();
  release_lcd();
}

static void dispenseProduct(int amount, int productNum) {
  assert(productNum >= 1 && productNum <= PRODUCT_COUNT);
  assert(amount >= 0);
  
  int priceIndex = productNum - 1;
  assert(priceIndex >= 0 && priceIndex < PRODUCT_COUNT);
  int price = productPrices[priceIndex];
  
  if (amount >= price) {
    int change = amount - price;
    assert(change >= 0);
    
    acquire_led();
    digitalWrite(LED_PIN, HIGH);
    release_led();
    
    acquire_lcd();
    lcd.clear();
    lcd.print("Dispensing...");
    release_lcd();
    delay(DELAY_SHORT);
    
    acquire_led();
    digitalWrite(LED_PIN, LOW);
    release_led();
    
    acquire_lcd();
    lcd.clear();
    lcd.print("Change: ");
    lcd.print(change);
    release_lcd();
  } else {
    acquire_lcd();
    lcd.clear();
    lcd.print("Not enough money");
    release_lcd();
    delay(DELAY_LONG);
    acquire_lcd();
    lcd.clear();
    release_lcd();
  }
}