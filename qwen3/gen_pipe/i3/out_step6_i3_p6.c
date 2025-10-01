#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Error code definitions
typedef enum {
    ERROR_NONE = 0,
    ERROR_NULL_POINTER,
    ERROR_PARSING,
    ERROR_INVALID_PRODUCT,
    ERROR_MUTEX_ACQUIRE,
    ERROR_INSUFFICIENT_FUND,
    ERROR_BUFFER_OVERFLOW
} ErrorCode;

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
static ErrorCode trimInput(char* input);
static ErrorCode parseInput(const char* input, int* amount, int* productNum);
static ErrorCode validateProduct(int productNum);
static ErrorCode displayInvalidFormat();
static ErrorCode displayInvalidProduct();
static ErrorCode displayNotEnoughMoney();
static ErrorCode dispenseProduct(int amount, int productNum);
static ErrorCode acquire_lcd();
static ErrorCode release_lcd();
static ErrorCode acquire_led();
static ErrorCode release_led();

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
        
        ErrorCode err = trimInput(inputBuffer);
        if (err != ERROR_NONE) {
            displayInvalidFormat();
            return;
        }
        
        int amount = 0, productNum = 0;
        err = parseInput(inputBuffer, &amount, &productNum);
        if (err != ERROR_NONE) {
            displayInvalidFormat();
            return;
        }
        
        if (amount < 0) {
            displayInvalidFormat();
            return;
        }
        
        err = validateProduct(productNum);
        if (err != ERROR_NONE) {
            displayInvalidProduct();
            return;
        }
        
        err = dispenseProduct(amount, productNum);
        if (err == ERROR_INSUFFICIENT_FUND) {
            displayNotEnoughMoney();
        } else if (err != ERROR_NONE) {
            // Handle other errors, fallback to safe state
            acquire_lcd();
            if (err == ERROR_MUTEX_ACQUIRE) {
                lcd.clear();
            }
            release_lcd();
        }
    }
}

static ErrorCode trimInput(char* input) {
    if (input == NULL) {
        return ERROR_NULL_POINTER;
    }

    char* start = input;
    while (*start == ' ') start++;
    
    char* end = start;
    while (*end != '\0') end++;
    end--;
    while (end >= start && *end == ' ') end--;
    *(end + 1) = '\0';
    
    memmove(input, start, end - start + 1);
    return ERROR_NONE;
}

static ErrorCode parseInput(const char* input, int* amount, int* productNum) {
    if (input == NULL || amount == NULL || productNum == NULL) {
        return ERROR_NULL_POINTER;
    }

    int parsed = sscanf(input, "%d %d", amount, productNum);
    if (parsed != 2) {
        return ERROR_PARSING;
    }
    return ERROR_NONE;
}

static ErrorCode validateProduct(int productNum) {
    if (productNum < 1 || productNum > PRODUCT_COUNT) {
        return ERROR_INVALID_PRODUCT;
    }
    return ERROR_NONE;
}

static ErrorCode acquire_lcd() {
    noInterrupts();
    uint32_t start = millis();
    while (lcd_mutex) {
        if (millis() - start > 100) { // 100ms timeout
            interrupts();
            return ERROR_MUTEX_ACQUIRE;
        }
    }
    lcd_mutex = 1;
    interrupts();
    return ERROR_NONE;
}

static ErrorCode release_lcd() {
    noInterrupts();
    if (lcd_mutex) {
        lcd_mutex = 0;
    } else {
        interrupts();
        return ERROR_MUTEX_ACQUIRE;
    }
    interrupts();
    return ERROR_NONE;
}

static ErrorCode acquire_led() {
    noInterrupts();
    uint32_t start = millis();
    while (led_mutex) {
        if (millis() - start > 100) { // 100ms timeout
            interrupts();
            return ERROR_MUTEX_ACQUIRE;
        }
    }
    led_mutex = 1;
    interrupts();
    return ERROR_NONE;
}

static ErrorCode release_led() {
    noInterrupts();
    if (led_mutex) {
        led_mutex = 0;
    } else {
        interrupts();
        return ERROR_MUTEX_ACQUIRE;
    }
    interrupts();
    return ERROR_NONE;
}

static ErrorCode displayInvalidFormat() {
    ErrorCode err = acquire_lcd();
    if (err != ERROR_NONE) return err;
    lcd.clear();
    lcd.print("Invalid format");
    release_lcd();
    delay(DELAY_LONG);
    err = acquire_lcd();
    if (err != ERROR_NONE) return err;
    lcd.clear();
    release_lcd();
    return ERROR_NONE;
}

static ErrorCode displayInvalidProduct() {
    ErrorCode err = acquire_lcd();
    if (err != ERROR_NONE) return err;
    lcd.clear();
    lcd.print("Invalid product");
    release_lcd();
    delay(DELAY_LONG);
    err = acquire_lcd();
    if (err != ERROR_NONE) return err;
    lcd.clear();
    release_lcd();
    return ERROR_NONE;
}

static ErrorCode displayNotEnoughMoney() {
    ErrorCode err = acquire_lcd();
    if (err != ERROR_NONE) return err;
    lcd.clear();
    lcd.print("Not enough money");
    release_lcd();
    delay(DELAY_LONG);
    err = acquire_lcd();
    if (err != ERROR_NONE) return err;
    lcd.clear();
    release_lcd();
    return ERROR_NONE;
}

static ErrorCode dispenseProduct(int amount, int productNum) {
    if (productNum < 1 || productNum > PRODUCT_COUNT) {
        return ERROR_INVALID_PRODUCT;
    }
    if (amount < 0) {
        return ERROR_PARSING;
    }

    int priceIndex = productNum - 1;
    int price = productPrices[priceIndex];

    if (amount < price) {
        return ERROR_INSUFFICIENT_FUND;
    }

    int change = amount - price;

    ErrorCode err = acquire_led();
    if (err != ERROR_NONE) return err;
    digitalWrite(LED_PIN, HIGH);
    release_led();

    err = acquire_lcd();
    if (err != ERROR_NONE) return err;
    lcd.clear();
    lcd.print("Dispensing...");
    release_lcd();
    delay(DELAY_SHORT);

    err = acquire_led();
    if (err != ERROR_NONE) return err;
    digitalWrite(LED_PIN, LOW);
    release_led();

    err = acquire_lcd();
    if (err != ERROR_NONE) return err;
    lcd.clear();
    lcd.print("Change: ");
    lcd.print(change);
    release_lcd();
    return ERROR_NONE;
}