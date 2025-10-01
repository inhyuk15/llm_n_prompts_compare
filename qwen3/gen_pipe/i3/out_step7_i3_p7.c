#include <Wire.h>
#include <LiquidCrystal_I2C.h>

/**
 * @brief Error codes used throughout the application.
 */
typedef enum {
    /** @brief No error occurred. */
    ERROR_NONE = 0,
    /** @brief Null pointer passed to a function. */
    ERROR_NULL_POINTER,
    /** @brief Error during input parsing. */
    ERROR_PARSING,
    /** @brief Invalid product number specified. */
    ERROR_INVALID_PRODUCT,
    /** @brief Failed to acquire a mutex. */
    ERROR_MUTEX_ACQUIRE,
    /** @brief Insufficient funds provided. */
    ERROR_INSUFFICIENT_FUND,
    /** @brief Buffer overflow detected. */
    ERROR_BUFFER_OVERFLOW
} ErrorCode;

/**
 * @brief I2C address of the LCD display.
 */
static const int LCD_ADDRESS = 0x27;

/**
 * @brief Number of columns on the LCD display.
 */
static const int LCD_COLS = 16;

/**
 * @brief Number of rows on the LCD display.
 */
static const int LCD_ROWS = 2;

/**
 * @brief GPIO pin for product dispensing LED.
 */
static const int LED_PIN = 2;

/**
 * @brief I2C SDA pin for Xiao-ESP32-S3.
 */
static const int I2C_SDA = 8;

/**
 * @brief I2C SCL pin for Xiao-ESP32-S3.
 */
static const int I2C_SCL = 9;

/**
 * @brief Price of product 1.
 */
static const int PRODUCT_PRICE_1 = 100;

/**
 * @brief Price of product 2.
 */
static const int PRODUCT_PRICE_2 = 150;

/**
 * @brief Price of product 3.
 */
static const int PRODUCT_PRICE_3 = 200;

/**
 * @brief Size of input buffer for serial commands.
 */
static const int INPUT_BUFFER_SIZE = 32;

/**
 * @brief Serial communication baud rate.
 */
static const int BAUD_RATE = 115200;

/**
 * @brief Short delay duration in milliseconds.
 */
static const int DELAY_SHORT = 1000;

/**
 * @brief Long delay duration in milliseconds.
 */
static const int DELAY_LONG = 2000;

/**
 * @brief Total number of available products.
 */
static const int PRODUCT_COUNT = 3;

/**
 * @brief Array of product prices.
 */
static const int productPrices[] = {PRODUCT_PRICE_1, PRODUCT_PRICE_2, PRODUCT_PRICE_3};

/**
 * @brief LCD display instance.
 */
static LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLS, LCD_ROWS);

/**
 * @brief Mutex for LCD access protection.
 */
static volatile uint8_t lcd_mutex = 0;

/**
 * @brief Mutex for LED access protection.
 */
static volatile uint8_t led_mutex = 0;

/**
 * @brief Initializes hardware and peripherals.
 *
 * Sets up LED pin, I2C communication, LCD display, and serial communication.
 */
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    Wire.begin(I2C_SDA, I2C_SCL);
    lcd.begin();
    lcd.backlight();
    Serial.begin(BAUD_RATE);
}

/**
 * @brief Main program loop.
 *
 * Reads serial input, processes commands, and dispenses products.
 */
void loop() {
    if (Serial.available()) {
        char inputBuffer[INPUT_BUFFER_SIZE] = {0};
        int bytesRead = Serial.readBytesUntil('\n', inputBuffer, sizeof(inputBuffer) - 1);
        inputBuffer[bytesRead] = '\0';
        
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
            acquire_lcd();
            if (err == ERROR_MUTEX_ACQUIRE) {
                lcd.clear();
            }
            release_lcd();
        }
    }
}

/**
 * @brief Trims whitespace from input string.
 *
 * @param input Pointer to input string to trim.
 * @return ErrorCode Returns ERROR_NONE on success, or appropriate error code.
 */
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

/**
 * @brief Parses input string into amount and product number.
 *
 * @param input Input string to parse.
 * @param amount Pointer to store parsed amount.
 * @param productNum Pointer to store parsed product number.
 * @return ErrorCode Returns ERROR_NONE on success, or appropriate error code.
 */
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

/**
 * @brief Validates product number against available products.
 *
 * @param productNum Product number to validate.
 * @return ErrorCode Returns ERROR_NONE if valid, or ERROR_INVALID_PRODUCT.
 */
static ErrorCode validateProduct(int productNum) {
    if (productNum < 1 || productNum > PRODUCT_COUNT) {
        return ERROR_INVALID_PRODUCT;
    }
    return ERROR_NONE;
}

/**
 * @brief Acquires LCD mutex with timeout.
 *
 * @return ErrorCode Returns ERROR_NONE on success, or ERROR_MUTEX_ACQUIRE.
 */
static ErrorCode acquire_lcd() {
    noInterrupts();
    uint32_t start = millis();
    while (lcd_mutex) {
        if (millis() - start > 100) {
            interrupts();
            return ERROR_MUTEX_ACQUIRE;
        }
    }
    lcd_mutex = 1;
    interrupts();
    return ERROR_NONE;
}

/**
 * @brief Releases LCD mutex.
 *
 * @return ErrorCode Returns ERROR_NONE on success, or ERROR_MUTEX_ACQUIRE.
 */
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

/**
 * @brief Acquires LED mutex with timeout.
 *
 * @return ErrorCode Returns ERROR_NONE on success, or ERROR_MUTEX_ACQUIRE.
 */
static ErrorCode acquire_led() {
    noInterrupts();
    uint32_t start = millis();
    while (led_mutex) {
        if (millis() - start > 100) {
            interrupts();
            return ERROR_MUTEX_ACQUIRE;
        }
    }
    led_mutex = 1;
    interrupts();
    return ERROR_NONE;
}

/**
 * @brief Releases LED mutex.
 *
 * @return ErrorCode Returns ERROR_NONE on success, or ERROR_MUTEX_ACQUIRE.
 */
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

/**
 * @brief Displays "Invalid format" message on LCD.
 *
 * @return ErrorCode Returns ERROR_NONE on success, or error code from mutex.
 */
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

/**
 * @brief Displays "Invalid product" message on LCD.
 *
 * @return ErrorCode Returns ERROR_NONE on success, or error code from mutex.
 */
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

/**
 * @brief Displays "Not enough money" message on LCD.
 *
 * @return ErrorCode Returns ERROR_NONE on success, or error code from mutex.
 */
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

/**
 * @brief Dispenses product and calculates change.
 *
 * @param amount Amount of money provided.
 * @param productNum Selected product number.
 * @return ErrorCode Returns ERROR_NONE on success, or appropriate error code.
 */
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