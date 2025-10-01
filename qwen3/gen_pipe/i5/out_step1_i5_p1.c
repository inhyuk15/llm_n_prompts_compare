#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>  // for strcasecmp and strlen
#include <ctype.h>   // for isspace

// Initialize LCD with I2C address 0x27, 16 columns, 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define MAX_CARS 10

// Parking system variables (static to limit scope)
static int currentCars = 0;
static unsigned long entryTimes[MAX_CARS];
static float totalFee = 0.0;

// Fee calculation constants
#define FEE_RATE 0.05  // $0.05 per minute

// Stack usage: 0 bytes
void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  // Initialize LCD
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Parking System");
  delay(2000);
  lcd.clear();
  
  updateLCD();
}

// Stack usage: 0 bytes
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
  lcd.print(totalFee, 2);
}

// Stack usage: 44 bytes (32-byte command buffer + 4+4+4 bytes for OUT command variables)
void loop() {
  // Command buffer with bounds checking
  char commandBuffer[32] = {0};
  
  if (Serial.available()) {
    // Read command with buffer overflow protection
    int bytesRead = Serial.readBytesUntil('\n', commandBuffer, sizeof(commandBuffer) - 1);
    if (bytesRead > 0) {
      // Trim trailing whitespace
      int len = strlen(commandBuffer);
      while (len > 0 && isspace(commandBuffer[len - 1])) {
        commandBuffer[len - 1] = '\0';
        len--;
      }
      
      // Process command
      if (strcasecmp(commandBuffer, "IN") == 0) {
        if (currentCars < MAX_CARS) {
          entryTimes[currentCars] = millis();
          currentCars++;
          
          // Static buffer for formatted output
          char buffer[32];
          snprintf(buffer, sizeof(buffer), "Car entered. Available spots: %d\n", MAX_CARS - currentCars);
          Serial.print(buffer);
        } else {
          Serial.println("Parking full! Cannot enter.");
        }
      } 
      else if (strcasecmp(commandBuffer, "OUT") == 0) {
        if (currentCars > 0) {
          // Calculate parking duration and fee
          unsigned long duration = millis() - entryTimes[0];
          float durationMinutes = duration / 60000.0;  // Convert ms to minutes
          float fee = durationMinutes * FEE_RATE;
          totalFee += fee;
          
          // Shift remaining cars in queue
          for (int i = 1; i < currentCars; i++) {
            entryTimes[i-1] = entryTimes[i];
          }
          currentCars--;
          
          // Static buffer for formatted output
          char buffer[32];
          snprintf(buffer, sizeof(buffer), "Parking fee: $%.2f\n", fee);
          Serial.print(buffer);
          snprintf(buffer, sizeof(buffer), "Car exited. Available spots: %d\n", MAX_CARS - currentCars);
          Serial.print(buffer);
        } else {
          Serial.println("No cars to exit!");
        }
      } 
      else {
        Serial.println("Invalid command. Use 'IN' or 'OUT'");
      }
    }
  }
  
  updateLCD();
  delay(1000);  // Refresh LCD every second
}