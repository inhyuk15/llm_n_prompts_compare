#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Named constants for magic numbers
#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2
#define NUM_FLOORS 5
#define DELAY_MS 500
#define INITIAL_ELEVATOR1_FLOOR 1
#define INITIAL_ELEVATOR2_FLOOR 5

// Button pins for each floor
#define FLOOR_BUTTON_PIN_1 34
#define FLOOR_BUTTON_PIN_2 35
#define FLOOR_BUTTON_PIN_3 36
#define FLOOR_BUTTON_PIN_4 37
#define FLOOR_BUTTON_PIN_5 38

// LCD setup
static LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

// Elevator positions
static int elevator1_floor = INITIAL_ELEVATOR1_FLOOR;
static int elevator2_floor = INITIAL_ELEVATOR2_FLOOR;

// Button pins array
static const int floor_button_pins[NUM_FLOORS] = {
  FLOOR_BUTTON_PIN_1,
  FLOOR_BUTTON_PIN_2,
  FLOOR_BUTTON_PIN_3,
  FLOOR_BUTTON_PIN_4,
  FLOOR_BUTTON_PIN_5
};

void setup() {
  // Stack usage: 4 bytes (loop variable i)
  // Initialize buttons
  for (int i = 0; i < NUM_FLOORS; i++) {
    pinMode(floor_button_pins[i], INPUT_PULLUP);
  }
  // Initialize LCD
  lcd.begin();
  lcd.backlight();
  update_lcd();
}

void update_lcd() {
  // Stack usage: 0 bytes
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
  // Stack usage: 8 bytes (parameters current_floor and target)
  while (*current_floor != target) {
    if (*current_floor < target) {
      (*current_floor)++;
    } else {
      (*current_floor)--;
    }
    update_lcd();
    delay(DELAY_MS); // Simulate movement time
  }
}

void loop() {
  // Stack usage: 16 bytes (i, target_floor, distance1, distance2)
  for (int i = 0; i < NUM_FLOORS; i++) {
    if (digitalRead(floor_button_pins[i]) == LOW) {
      int target_floor = i + 1;
      int distance1 = abs(elevator1_floor - target_floor);
      int distance2 = abs(elevator2_floor - target_floor);
      
      if (distance1 <= distance2) {
        move_elevator(&elevator1_floor, target_floor);
      } else {
        move_elevator(&elevator2_floor, target_floor);
      }
      
      delay(DELAY_MS); // Debounce
    }
  }
}