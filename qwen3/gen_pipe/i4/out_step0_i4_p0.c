#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD setup (adjust address if necessary)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Elevator positions
int elevator1_floor = 1;
int elevator2_floor = 5;

// Button pins for floors 1-5 (adjust according to your hardware)
const int floor_button_pins[5] = {34, 35, 36, 37, 38};

void setup() {
  // Initialize buttons
  for (int i = 0; i < 5; i++) {
    pinMode(floor_button_pins[i], INPUT_PULLUP);
  }
  // Initialize LCD
  lcd.begin();
  lcd.backlight();
  update_lcd();
}

void update_lcd() {
  lcd.setCursor(0, 0);
  lcd.print("E1: ");
  lcd.print(elevator1_floor);
  lcd.print("          ");
  lcd.setCursor(0, 1);
  lcd.print("E2: ");
  lcd.print(elevator2_floor);
  lcd.print("          ");
}

void move_elevator(int *current_floor, int target) {
  while (*current_floor != target) {
    if (*current_floor < target) {
      (*current_floor)++;
    } else {
      (*current_floor)--;
    }
    update_lcd();
    delay(500); // Simulate movement time
  }
}

void loop() {
  for (int i = 0; i < 5; i++) {
    if (digitalRead(floor_button_pins[i]) == LOW) {
      int target_floor = i + 1;
      int distance1 = abs(elevator1_floor - target_floor);
      int distance2 = abs(elevator2_floor - target_floor);
      
      if (distance1 <= distance2) {
        move_elevator(&elevator1_floor, target_floor);
      } else {
        move_elevator(&elevator2_floor, target_floor);
      }
      
      delay(500); // Debounce
    }
  }
}