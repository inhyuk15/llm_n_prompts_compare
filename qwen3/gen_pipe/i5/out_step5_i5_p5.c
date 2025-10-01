#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>  // for strcasecmp and strlen
#include <ctype.h>   // for isspace

// Function prototypes
void trimString(char* str);
void processCommand(char* command);
void handleCarIn(void);
void handleCarOut(void);

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

// Fee calculation constants
#define FEE_RATE 0.05  // $0.05 per minute
#define MAX_CARS 10

// Initialize LCD with I2C address, columns, and rows
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

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
  
  updateLCD();
}

void updateLCD() {
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

void loop() {
  // Command buffer with bounds checking
  char commandBuffer[CMD_BUFFER_SIZE] = {0};
  
  if (Serial.available()) {
    // Read command with buffer overflow protection
    int bytesRead = Serial.readBytesUntil('\n', commandBuffer, sizeof(commandBuffer) - 1);
    if (bytesRead > 0) {
      // Trim trailing whitespace
      trimString(commandBuffer);
      processCommand(commandBuffer);
    }
  }
  
  updateLCD();
  delay(LCD_REFRESH_DELAY_MS);  // Refresh LCD every second
}

void trimString(char* str) {
  size_t len = strlen(str);
  while (len > 0 && isspace((unsigned char)str[len - 1])) {
    str[len - 1] = '\0';
    len--;
  }
}

void processCommand(char* command) {
  if (strcasecmp(command, "IN") == 0) {
    handleCarIn();
  } else if (strcasecmp(command, "OUT") == 0) {
    handleCarOut();
  } else {
    Serial.println("Invalid command. Use 'IN' or 'OUT'");
  }
}

void handleCarIn() {
  noInterrupts();
  if (currentCars < MAX_CARS) {
    entryTimes[currentCars] = millis();
    currentCars++;
    interrupts();
    
    char buffer[CMD_BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "Car entered. Available spots: %d\n", MAX_CARS - currentCars);
    Serial.print(buffer);
  } else {
    interrupts();
    Serial.println("Parking full! Cannot enter.");
  }
}

void handleCarOut() {
  noInterrupts();
  if (currentCars > 0) {
    unsigned long currentMillis = millis();
    if (entryTimes[0] > currentMillis) {
      interrupts();
      Serial.println("Invalid entry time detected.");
      return;
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
  } else {
    interrupts();
    Serial.println("No cars to exit!");
  }
}