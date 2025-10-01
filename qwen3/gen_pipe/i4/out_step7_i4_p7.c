#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <stdbool.h>

/**
 * @brief Error codes used throughout the system
 */
typedef enum {
    ERROR_NONE = 0,              /**< No error occurred */
    ERROR_INVALID_PARAMETER,     /**< Invalid function parameter */
    ERROR_NULL_POINTER,          /**< NULL pointer passed to function */
    ERROR_OUT_OF_RANGE,          /**< Value out of valid range */
    ERROR_HARDWARE_FAILURE,      /**< Hardware-related failure */
    ERROR_FATAL,                 /**< Unrecoverable system error */
    ERROR_RECOVERABLE            /**< Recoverable error condition */
} ErrorCode;

/**
 * @brief System configuration constants
 */
#define LCD_ADDRESS 0x27        /**< I2C address of LCD display */
#define LCD_COLUMNS 16          /**< Number of columns on LCD */
#define LCD_ROWS 2              /**< Number of rows on LCD */
#define NUM_FLOORS 5            /**< Total number of floors */
#define DELAY_MS 500            /**< Movement delay in milliseconds */
#define INITIAL_ELEVATOR1_FLOOR 1 /**< Starting position for elevator 1 */
#define INITIAL_ELEVATOR2_FLOOR 5 /**< Starting position for elevator 2 */

/**
 * @brief Floor button pin assignments
 */
#define FLOOR_BUTTON_PIN_1 34   /**< Button for floor 1 */
#define FLOOR_BUTTON_PIN_2 35   /**< Button for floor 2 */
#define FLOOR_BUTTON_PIN_3 36   /**< Button for floor 3 */
#define FLOOR_BUTTON_PIN_4 37   /**< Button for floor 4 */
#define FLOOR_BUTTON_PIN_5 38   /**< Button for floor 5 */

/**
 * @brief LCD display instance
 */
static LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

/**
 * @brief Current positions of elevators (volatile for interrupt safety)
 */
static volatile int elevator1_floor = INITIAL_ELEVATOR1_FLOOR;
static volatile int elevator2_floor = INITIAL_ELEVATOR2_FLOOR;

/**
 * @brief Array mapping floor indices to button pins
 */
static const int floor_button_pins[NUM_FLOORS] = {
  FLOOR_BUTTON_PIN_1,
  FLOOR_BUTTON_PIN_2,
  FLOOR_BUTTON_PIN_3,
  FLOOR_BUTTON_PIN_4,
  FLOOR_BUTTON_PIN_5
};

/**
 * @brief Checks if a floor button is pressed
 * 
 * @param i Index of the floor (0-based)
 * @param pressed Pointer to store button state
 * @return ErrorCode ERROR_NONE if successful, otherwise error code
 */
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

/**
 * @brief Determines which elevator is closer to a target floor
 * 
 * @param target_floor Desired floor (1-based)
 * @param elevator_to_move Pointer to store selected elevator (1 or 2)
 * @return ErrorCode ERROR_NONE if successful, otherwise error code
 */
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

/**
 * @brief Moves the specified elevator to a target floor
 * 
 * @param elevator_num Elevator number (1 or 2)
 * @param target_floor Target floor (1-based)
 * @return ErrorCode ERROR_NONE if successful, otherwise error code
 */
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

/**
 * @brief Moves an elevator to a target floor with position updates
 * 
 * @param current_floor Pointer to current elevator position
 * @param target Target floor (1-based)
 * @return ErrorCode ERROR_NONE if successful, otherwise error code
 */
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

/**
 * @brief Updates LCD display with current elevator positions
 * 
 * @return ErrorCode ERROR_NONE if successful, otherwise error code
 */
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

/**
 * @brief Processes a button press for the specified floor
 * 
 * @param i Index of the floor (0-based)
 * @return ErrorCode ERROR_NONE if successful, otherwise error code
 */
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

/**
 * @brief Initializes hardware and peripherals
 */
void setup() {
  for (int i = 0; i < NUM_FLOORS; i++) {
    pinMode(floor_button_pins[i], INPUT_PULLUP);
  }
  lcd.begin();
  lcd.backlight();
  update_lcd();
}

/**
 * @brief Scans all floor buttons for presses
 */
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

/**
 * @brief Main program loop
 */
void loop() {
  handle_floor_buttons();
}