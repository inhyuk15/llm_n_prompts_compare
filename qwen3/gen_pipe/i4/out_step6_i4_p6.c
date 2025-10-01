#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <stdbool.h>

typedef enum {
    ERROR_NONE = 0,
    ERROR_INVALID_PARAMETER,
    ERROR_NULL_POINTER,
    ERROR_OUT_OF_RANGE,
    ERROR_HARDWARE_FAILURE,
    ERROR_FATAL,
    ERROR_RECOVERABLE
} ErrorCode;

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
static volatile int elevator1_floor = INITIAL_ELEVATOR1_FLOOR;
static volatile int elevator2_floor = INITIAL_ELEVATOR2_FLOOR;

// Button pins array
static const int floor_button_pins[NUM_FLOORS] = {
  FLOOR_BUTTON_PIN_1,
  FLOOR_BUTTON_PIN_2,
  FLOOR_BUTTON_PIN_3,
  FLOOR_BUTTON_PIN_4,
  FLOOR_BUTTON_PIN_5
};

ErrorCode is_button_pressed(int i, bool *pressed) {
    if (i < 0 || i >= NUM_FLOORS) {
        return ERROR_INVALID_PARAMETER;
    }
    if (pressed == NULL) {
        return ERROR_NULL_POINTER;
    }
    *pressed = digitalRead(floor_button_pins[i]) == LOW;
    return ERROR_NONE;
}

ErrorCode get_closest_elevator(int target_floor, int *elevator_to_move) {
    if (target_floor < 1 || target_floor > NUM_FLOORS) {
        return ERROR_OUT_OF_RANGE;
    }
    if (elevator_to_move == NULL) {
        return ERROR_NULL_POINTER;
    }
    int e1, e2;
    noInterrupts();
    e1 = elevator1_floor;
    e2 = elevator2_floor;
    interrupts();
    int distance1 = abs(e1 - target_floor);
    int distance2 = abs(e2 - target_floor);
    *elevator_to_move = (distance1 <= distance2) ? 1 : 2;
    return ERROR_NONE;
}

ErrorCode move_selected_elevator(int elevator_num, int target_floor) {
    if (elevator_num != 1 && elevator_num != 2) {
        return ERROR_INVALID_PARAMETER;
    }
    if (target_floor < 1 || target_floor > NUM_FLOORS) {
        return ERROR_OUT_OF_RANGE;
    }
    if (elevator_num == 1) {
        return move_elevator(&elevator1_floor, target_floor);
    } else {
        return move_elevator(&elevator2_floor, target_floor);
    }
}

ErrorCode move_elevator(volatile int *current_floor, int target) {
    if (current_floor == NULL) {
        return ERROR_NULL_POINTER;
    }
    if (target < 1 || target > NUM_FLOORS) {
        return ERROR_OUT_OF_RANGE;
    }
    
    while (true) {
        int current;
        noInterrupts();
        current = *current_floor;
        interrupts();
        if (current == target) {
            break;
        }
        
        noInterrupts();
        if (current < target) {
            (*current_floor)++;
        } else {
            (*current_floor)--;
        }
        // Clamp to valid floor range
        if (*current_floor < 1) *current_floor = 1;
        if (*current_floor > NUM_FLOORS) *current_floor = NUM_FLOORS;
        interrupts();
        
        ErrorCode ec = update_lcd();
        if (ec != ERROR_NONE) {
            // Handle recoverable error
        }
        
        delay(DELAY_MS);
    }
    return ERROR_NONE;
}

ErrorCode update_lcd() {
    int e1, e2;
    noInterrupts();
    e1 = elevator1_floor;
    e2 = elevator2_floor;
    interrupts();
    
    lcd.setCursor(0, 0);
    lcd.print("E1: ");
    lcd.print(e1);
    lcd.print("          ");
    lcd.setCursor(0, 1);
    lcd.print("E2: ");
    lcd.print(e2);
    lcd.print("          ");
    
    return ERROR_NONE;
}

ErrorCode process_button(int i) {
    if (i < 0 || i >= NUM_FLOORS) {
        return ERROR_INVALID_PARAMETER;
    }
    int target_floor = i + 1;
    int elevator_to_move;
    ErrorCode ec = get_closest_elevator(target_floor, &elevator_to_move);
    if (ec != ERROR_NONE) {
        return ec;
    }
    if (elevator_to_move != 1 && elevator_to_move != 2) {
        return ERROR_INVALID_PARAMETER;
    }
    return move_selected_elevator(elevator_to_move, target_floor);
}

void setup() {
  for (int i = 0; i < NUM_FLOORS; i++) {
    pinMode(floor_button_pins[i], INPUT_PULLUP);
  }
  lcd.begin();
  lcd.backlight();
  update_lcd();
}

void handle_floor_buttons() {
  for (int i = 0; i < NUM_FLOORS; i++) {
    bool pressed;
    ErrorCode ec = is_button_pressed(i, &pressed);
    if (ec != ERROR_NONE) {
        continue;
    }
    if (pressed) {
        ec = process_button(i);
        if (ec != ERROR_NONE) {
            // Handle error
        }
    }
  }
}

void loop() {
  handle_floor_buttons();
}