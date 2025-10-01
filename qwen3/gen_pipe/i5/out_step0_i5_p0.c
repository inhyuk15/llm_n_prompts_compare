#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Initialize LCD with I2C address 0x27, 16 columns, 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define MAX_CARS 10

// Parking system variables
int currentCars = 0;
unsigned long entryTimes[MAX_CARS];
float totalFee = 0.0;

// Fee calculation constants
#define FEE_RATE 0.05  // $0.05 per minute

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

void loop() {
  // Check for serial input
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.equalsIgnoreCase("IN")) {
      if (currentCars < MAX_CARS) {
        entryTimes[currentCars] = millis();
        currentCars++;
        Serial.println("Car entered. Available spots: " + String(MAX_CARS - currentCars));
      } else {
        Serial.println("Parking full! Cannot enter.");
      }
    } 
    else if (command.equalsIgnoreCase("OUT")) {
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
        
        Serial.print("Parking fee: $");
        Serial.println(fee, 2);
        Serial.println("Car exited. Available spots: " + String(MAX_CARS - currentCars));
      } else {
        Serial.println("No cars to exit!");
      }
    } 
    else {
      Serial.println("Invalid command. Use 'IN' or 'OUT'");
    }
  }
  
  updateLCD();
  delay(1000);  // Refresh LCD every second
}