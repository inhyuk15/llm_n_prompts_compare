#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>  // for strcasecmp and strlen
#include <ctype.h>   // for isspace

// Error codes for systematic error handling
typedef enum {
    ERROR_NONE = 0,
    ERROR_INVALID_COMMAND,
    ERROR_PARKING_FULL,
    ERROR_NO_CARS_EXIT,
    ERROR_INVALID_ENTRY_TIME,
    ERROR_LCD_INIT_FAILED,
    ERROR_SERIAL_READ,
    ERROR_BUFFER_OVERFLOW
} ErrorCode;

// Function prototypes
ErrorCode trimString(char* str);
ErrorCode processCommand(char* command);
ErrorCode handleCarIn(void);
ErrorCode handleCarOut(void);
void updateLCD(void);

// Constants for configuration
#define CMD_BUFFER_SIZE 32
#define LCD_I2C_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2
#define MS_PER_MINUTE 60000.0F
#define LCD_REFRESH_DELAY_MS 1000
#define FEE_DECIMAL_PLACES 2
#define BAUD_RATE 115200
#define LCD_INIT_DELAY_MS 2000

// Parking system variables
static volatile int currentCars = 0;
static volatile unsigned long entryTimes[MAX_CARS];
static volatile float totalFee = 0.0;
static const int MAX_CARS = 10;

// Fee calculation constants
#define FEE_RATE 0.05  // $0.05 per minute

// System status
static ErrorCode systemError = ERROR_NONE;

// Initialize LCD with I2C address, columns, and rows
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

// System initialization
void setup() {
    Serial.begin(BAUD_RATE);
    Wire.begin();
    
    // Initialize LCD
    lcd.begin();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Parking System");
    delay(LCD_INIT_DELAY_MS);
    lcd.clear();
    
    // Check I2C communication with LCD
    Wire.beginTransmission(LCD_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        systemError = ERROR_LCD_INIT_FAILED;
    }
    
    updateLCD();
}

void loop() {
    // Command buffer with bounds checking
    char commandBuffer[CMD_BUFFER_SIZE] = {0};
    
    if (Serial.available()) {
        // Read command with buffer overflow protection
        int bytesRead = Serial.readBytesUntil('\n', commandBuffer, sizeof(commandBuffer) - 1);
        if (bytesRead > 0) {
            // Ensure null-termination
            commandBuffer[sizeof(commandBuffer) - 1] = '\0';
            
            // Trim trailing whitespace
            ErrorCode ec = trimString(commandBuffer);
            if (ec != ERROR_NONE) {
                Serial.println("Error trimming command.");
                return;
            }
            
            // Check for empty command
            if (strlen(commandBuffer) == 0) {
                Serial.println("Empty command.");
                return;
            }
            
            // Process command
            ec = processCommand(commandBuffer);
            if (ec != ERROR_NONE) {
                // Log recoverable error but continue operation
                return;
            }
        } else {
            systemError = ERROR_SERIAL_READ;
        }
    }
    
    updateLCD();
    delay(LCD_REFRESH_DELAY_MS);  // Refresh LCD every second
}

// Trims trailing whitespace from a string
ErrorCode trimString(char* str) {
    if (str == NULL) {
        return ERROR_BUFFER_OVERFLOW;
    }
    
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
    return ERROR_NONE;
}

// Processes incoming commands
ErrorCode processCommand(char* command) {
    if (strcasecmp(command, "IN") == 0) {
        return handleCarIn();
    } else if (strcasecmp(command, "OUT") == 0) {
        return handleCarOut();
    } else {
        Serial.println("Invalid command. Use 'IN' or 'OUT'");
        return ERROR_INVALID_COMMAND;
    }
}

// Handles car entry
ErrorCode handleCarIn() {
    noInterrupts();
    if (currentCars < MAX_CARS) {
        entryTimes[currentCars] = millis();
        currentCars++;
        interrupts();
        
        char buffer[CMD_BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "Car entered. Available spots: %d\n", MAX_CARS - currentCars);
        Serial.print(buffer);
        return ERROR_NONE;
    } else {
        interrupts();
        return ERROR_PARKING_FULL;
    }
}

// Handles car exit
ErrorCode handleCarOut() {
    noInterrupts();
    if (currentCars > 0) {
        unsigned long currentMillis = millis();
        if (entryTimes[0] > currentMillis) {
            interrupts();
            return ERROR_INVALID_ENTRY_TIME;
        }
        
        unsigned long duration = currentMillis - entryTimes[0];
        float durationMinutes = (float)duration / MS_PER_MINUTE;  // Convert ms to minutes
        float fee = durationMinutes * FEE_RATE;
        
        // Shift remaining cars in queue
        for (int i = 1; i < currentCars; i++) {
            entryTimes[i-1] = entryTimes[i];
        }
        currentCars--;
        totalFee += fee;
        interrupts();
        
        char buffer[CMD_BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "Parking fee: $%.2f\n", fee);
        Serial.print(buffer);
        snprintf(buffer, sizeof(buffer), "Car exited. Available spots: %d\n", MAX_CARS - currentCars);
        Serial.print(buffer);
        return ERROR_NONE;
    } else {
        interrupts();
        return ERROR_NO_CARS_EXIT;
    }
}

// Updates LCD display with current system status
void updateLCD() {
    if (systemError != ERROR_NONE) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Error: ");
        switch (systemError) {
            case ERROR_LCD_INIT_FAILED:
                lcd.print("LCD Fail");
                break;
            case ERROR_SERIAL_READ:
                lcd.print("Serial Err");
                break;
            default:
                lcd.print("Unknown");
        }
        return;
    }
    
    // Display remaining parking spots
    lcd.setCursor(0, 0);
    lcd.print("Spots: ");
    lcd.print(currentCars);
    lcd.print("/");
    lcd.print(MAX_CARS);
    
    // Display total fees
    lcd.setCursor(0, 1);
    lcd.print("Fee: $");
    lcd.print(totalFee, FEE_DECIMAL_PLACES);
}