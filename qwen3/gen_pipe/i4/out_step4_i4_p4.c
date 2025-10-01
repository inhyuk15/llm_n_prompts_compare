#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <stdbool.h>
#include <assert.h>

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
  for (int i = 0; i < NUM_FLOORS; i++) {
    assert(i >= 0 && i < NUM_FLOORS);
    pinMode(floor_button_pins[i], INPUT_PULLUP);
  }
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
  assert(current_floor != NULL);
  assert(target >= 1 && target <= NUM_FLOORS);
  
  while (*current_floor != target) {
    if (*current_floor < target) {
      (*current_floor)++;
    } else {
      (*current_floor)--;
    }
    
    // Clamp to valid floor range
    if (*current_floor < 1) *current_floor = 1;
    if (*current_floor > NUM_FLOORS) *current_floor = NUM_FLOORS;
    
    update_lcd();
    delay(DELAY_MS);
  }
}

bool is_button_pressed(int i) {
  assert(i >= 0 && i < NUM_FLOORS);
  return digitalRead(floor_button_pins[i]) == LOW;
}

void process_button(int i) {
  assert(i >= 0 && i < NUM_FLOORS);
  int target_floor = i + 1;
  int elevator_to_move = get_closest_elevator(target_floor);
  assert(elevator_to_move == 1 || elevator_to_move == 2);
  move_selected_elevator(elevator_to_move, target_floor);
  delay(DELAY_MS);
}

int get_closest_elevator(int target_floor) {
  assert(target_floor >= 1 && target_floor <= NUM_FLOORS);
  int distance1 = abs(elevator1_floor - target_floor);
  int distance2 = abs(elevator2_floor - target_floor);
  return (distance1 <= distance2) ? 1 : 2;
}

void move_selected_elevator(int elevator_num, int target_floor) {
  assert(elevator_num == 1 || elevator_num == 2);
  assert(target_floor >= 1 && target_floor <= NUM_FLOORS);
  
  if (elevator_num == 1) {
    move_elevator(&elevator1_floor, target_floor);
  } else {
    move_elevator(&elevator2_floor, target_floor);
  }
}

void handle_floor_buttons() {
  for (int i = 0; i < NUM_FLOORS; i++) {
    assert(i >= 0 && i < NUM_FLOORS);
    if (is_button_pressed(i)) {
      process_button(i);
    }
  }
}

void loop() {
  handle_floor_buttons();
}