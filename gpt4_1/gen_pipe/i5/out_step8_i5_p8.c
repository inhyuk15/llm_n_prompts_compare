#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>

#define MAX_CARS                (10U)
#define PARKING_FEE_PER_HOUR    (1000)
#define MAX_INPUT_LENGTH        (32U)
#define LCD_LINE_LENGTH         (16U)
#define MIN_HOUR                (0)
#define MAX_HOUR                (23)
#define MIN_MINUTE              (0)
#define MAX_MINUTE              (59)
#define MAX_TOTAL_MINUTES       (1440U)
#define SERIAL_BAUD_RATE        (115200U)

/**
 * @enum ErrorCode
 * @brief Error codes for parking system operations.
 */
typedef enum {
  ERR_OK = 0,             /**< Operation successful */
  ERR_INVALID_ARGUMENT,   /**< Invalid argument error */
  ERR_PARKING_FULL,       /**< Parking lot is full */
  ERR_PARKING_EMPTY,      /**< Parking lot is empty */
  ERR_TIME_INVALID,       /**< Invalid time input */
  ERR_FEE_CALC,           /**< Fee calculation error */
  ERR_BUFFER_OVERFLOW,    /**< Buffer overflow error */
  ERR_NOT_FOUND           /**< Requested item not found */
} ErrorCode;

/**
 * @struct ParkingSlot
 * @brief Represents a parking slot's status and timing information.
 */
typedef struct {
  volatile bool occupied;   /**< Status whether the slot is occupied */
  volatile int in_hour;     /**< Entry hour (0..23) */
  volatile int in_minute;   /**< Entry minute (0..59) */
  volatile int fee;         /**< Calculated parking fee */
} ParkingSlot;

static LiquidCrystal_I2C s_lcd(0x27U, LCD_LINE_LENGTH, 2U); /**< LCD display instance */
static volatile ParkingSlot s_parking_slots[MAX_CARS] = {0}; /**< Parking slots array */

/**
 * @brief Counts the number of currently parked cars.
 * @param[out] count Pointer to store the counted number of parked cars.
 * @return ErrorCode ERR_OK on success, ERR_INVALID_ARGUMENT if count is NULL.
 */
static ErrorCode parked_cars_count(int32_t *count)
{
  if (count == NULL) {
    return ERR_INVALID_ARGUMENT;
  }

  int32_t local_count = 0;
  noInterrupts();
  for (uint32_t i = 0U; i < MAX_CARS; i++) {
    if (s_parking_slots[i].occupied) {
      local_count++;
    }
  }
  interrupts();

  if (local_count < 0 || local_count > (int32_t)MAX_CARS) {
    *count = 0;
    return ERR_INVALID_ARGUMENT;
  }

  *count = local_count;
  return ERR_OK;
}

/**
 * @brief Calculates parking fee from entry and exit times.
 * @param[in] in_h Entry hour (0..23).
 * @param[in] in_m Entry minute (0..59).
 * @param[in] out_h Exit hour (0..23).
 * @param[in] out_m Exit minute (0..59).
 * @param[out] fee Pointer to integer to store calculated fee.
 * @return ErrorCode ERR_OK on success or appropriate error code.
 */
static ErrorCode calculate_fee(int32_t in_h, int32_t in_m, int32_t out_h, int32_t out_m, int32_t *fee)
{
  if (fee == NULL) {
    return ERR_INVALID_ARGUMENT;
  }

  if (in_h < MIN_HOUR || in_h > MAX_HOUR || out_h < MIN_HOUR || out_h > MAX_HOUR ||
      in_m < MIN_MINUTE || in_m > MAX_MINUTE || out_m < MIN_MINUTE || out_m > MAX_MINUTE) {
    *fee = 0;
    return ERR_TIME_INVALID;
  }

  if (in_h > (INT32_MAX / 60) || out_h > (INT32_MAX / 60)) {
    *fee = 0;
    return ERR_TIME_INVALID;
  }

  uint32_t in_total = (uint32_t)in_h * 60U + (uint32_t)in_m;
  uint32_t out_total = (uint32_t)out_h * 60U + (uint32_t)out_m;

  if (out_total < in_total || out_total > MAX_TOTAL_MINUTES) {
    *fee = 0;
    return ERR_TIME_INVALID;
  }
  uint32_t diff = out_total - in_total;
  uint32_t hours = diff / 60U;
  if ((diff % 60U) != 0U) {
    hours++;
  }

  if (hours > (UINT32_MAX / (uint32_t)PARKING_FEE_PER_HOUR)) {
    *fee = 0;
    return ERR_FEE_CALC;
  }

  int64_t fee64 = (int64_t)hours * PARKING_FEE_PER_HOUR;
  if (fee64 > INT32_MAX) {
    *fee = 0;
    return ERR_FEE_CALC;
  }

  *fee = (int32_t)fee64;
  return ERR_OK;
}

/**
 * @brief Displays remaining parking slots on LCD.
 */
static void print_remaining_slots(void)
{
  int32_t remaining = 0;
  if (parked_cars_count(&remaining) != ERR_OK) {
    remaining = 0;
  } else {
    remaining = (int32_t)MAX_CARS - remaining;
  }
  if (remaining < 0) {
    remaining = 0;
  } else if (remaining > (int32_t)MAX_CARS) {
    remaining = (int32_t)MAX_CARS;
  }

  char line[LCD_LINE_LENGTH + 1U];
  int n = snprintf(line, sizeof(line), "남은 주차: %2d 대", remaining);
  if (n <= 0 || (size_t)n >= sizeof(line)) {
    strncpy(line, "남은 주차: ?? 대", sizeof(line));
    line[sizeof(line) - 1] = '\0';
  }
  s_lcd.clear();
  s_lcd.setCursor(0, 0);
  s_lcd.print(line);
  s_lcd.setCursor(0, 1);
  s_lcd.print("입력: I, 출입: O");
}

/**
 * @brief Shows a temporary message on the LCD for specified delay.
 * @param[in] line1 First line string.
 * @param[in] line2 Second line string.
 * @param[in] delay_ms Delay duration in milliseconds.
 */
static void show_temporary_lcd(const char *line1, const char *line2, uint32_t delay_ms)
{
  if (line1 == NULL || line2 == NULL) {
    return;
  }
  s_lcd.clear();
  s_lcd.setCursor(0, 0);
  s_lcd.print(line1);
  s_lcd.setCursor(0, 1);
  s_lcd.print(line2);
  delay(delay_ms);
  print_remaining_slots();
}

/**
 * @brief Parses time from input string in format "COMMAND HH MM".
 * @param[in] line Input string.
 * @param[out] hour Pointer to store parsed hour.
 * @param[out] minute Pointer to store parsed minute.
 * @return true if parsing succeeds and values are valid, false otherwise.
 */
static bool parse_time(const char *line, int *hour, int *minute)
{
  if (line == NULL || hour == NULL || minute == NULL) {
    return false;
  }
  int count = sscanf(line, "%*c %d %d", hour, minute);
  if (count != 2) {
    return false;
  }
  if (*hour < MIN_HOUR || *hour > MAX_HOUR || *minute < MIN_MINUTE || *minute > MAX_MINUTE) {
    return false;
  }
  return true;
}

/**
 * @brief Processes car entry with specified time.
 * @param[in] hour Entry hour (0..23).
 * @param[in] minute Entry minute (0..59).
 * @return ErrorCode indicating success or failure reason.
 */
static ErrorCode process_entry(int hour, int minute)
{
  if (hour < MIN_HOUR || hour > MAX_HOUR || minute < MIN_MINUTE || minute > MAX_MINUTE) {
    return ERR_INVALID_ARGUMENT;
  }

  noInterrupts();
  int32_t count = 0;
  ErrorCode err = parked_cars_count(&count);
  if (err != ERR_OK || count >= (int32_t)MAX_CARS) {
    interrupts();
    Serial.println("만차입니다. 입차 불가.");
    show_temporary_lcd("만차입니다!", "", 2000);
    return ERR_PARKING_FULL;
  }

  for (int i = 0; i < (int32_t)MAX_CARS; i++) {
    if (!s_parking_slots[i].occupied) {
      s_parking_slots[i].occupied = true;
      s_parking_slots[i].in_hour = hour;
      s_parking_slots[i].in_minute = minute;
      s_parking_slots[i].fee = 0;
      interrupts();

      Serial.print("차량 #");
      Serial.print(i + 1);
      Serial.printf(" 입차 %02d:%02d\n", hour, minute);

      if (parked_cars_count(&count) != ERR_OK) {
        count = 0;
      }
      int32_t remain = (int32_t)MAX_CARS - count;
      if (remain < 0) {
        remain = 0;
      } else if (remain > (int32_t)MAX_CARS) {
        remain = (int32_t)MAX_CARS;
      }

      char line2[LCD_LINE_LENGTH + 1U];
      int n = snprintf(line2, sizeof(line2), "남은: %2d 대", remain);
      if (n <= 0 || (size_t)n >= sizeof(line2)) {
        strncpy(line2, "남은: ?? 대", sizeof(line2));
        line2[sizeof(line2) - 1] = '\0';
      }

      s_lcd.clear();
      s_lcd.setCursor(0, 0);
      s_lcd.print("차량 입차됨");
      s_lcd.setCursor(0, 1);
      s_lcd.print(line2);

      delay(2000);
      print_remaining_slots();
      return ERR_OK;
    }
  }
  interrupts();
  return ERR_PARKING_FULL; /* Should not reach here if count check is correct */
}

/**
 * @brief Finds the index of the oldest parked car.
 * @param[out] index Pointer to store found index.
 * @return ErrorCode ERR_OK if found, ERR_INVALID_ARGUMENT if index is NULL,
 *         ERR_NOT_FOUND if no parked cars.
 */
static ErrorCode find_oldest_parked_car_index(int *index)
{
  if (index == NULL) {
    return ERR_INVALID_ARGUMENT;
  }

  int32_t oldest_idx = -1;
  uint32_t oldest_time = MAX_TOTAL_MINUTES + 1U;

  noInterrupts();
  for (int i = 0; (uint32_t)i < MAX_CARS; i++) {
    if (s_parking_slots[i].occupied) {
      int ih = s_parking_slots[i].in_hour;
      int im = s_parking_slots[i].in_minute;
      if (ih < MIN_HOUR || ih > MAX_HOUR || im < MIN_MINUTE || im > MAX_MINUTE) {
        continue;
      }
      uint32_t in_time = (uint32_t)ih * 60U + (uint32_t)im;
      if (in_time < oldest_time) {
        oldest_time = in_time;
        oldest_idx = i;
      }
    }
  }
  interrupts();

  if (oldest_idx == -1) {
    return ERR_NOT_FOUND;
  }

  *index = oldest_idx;
  return ERR_OK;
}

/**
 * @brief Processes car exit with specified time, calculates fee, and updates slot.
 * @param[in] hour Exit hour (0..23).
 * @param[in] minute Exit minute (0..59).
 * @return ErrorCode indicating success or failure reason.
 */
static ErrorCode process_exit(int hour, int minute)
{
  if (hour < MIN_HOUR || hour > MAX_HOUR || minute < MIN_MINUTE || minute > MAX_MINUTE) {
    return ERR_INVALID_ARGUMENT;
  }

  int32_t count = 0;
  if (parked_cars_count(&count) != ERR_OK) {
    Serial.println("주차된 차량 데이터 오류.");
    show_temporary_lcd("데이터 오류", "", 2000);
    return ERR_PARKING_EMPTY;
  }
  if (count == 0) {
    Serial.println("주차된 차량이 없습니다.");
    show_temporary_lcd("주차 차량 없음", "", 2000);
    return ERR_PARKING_EMPTY;
  }

  int oldest_idx = -1;
  ErrorCode err = find_oldest_parked_car_index(&oldest_idx);
  if (err != ERR_OK || oldest_idx == -1) {
    Serial.println("출차할 차량이 없습니다.");
    show_temporary_lcd("출차 차량없음", "", 2000);
    return ERR_NOT_FOUND;
  }

  int slot_in_hour = s_parking_slots[oldest_idx].in_hour;
  int slot_in_minute = s_parking_slots[oldest_idx].in_minute;

  int fee = 0;
  err = calculate_fee(slot_in_hour, slot_in_minute, hour, minute, &fee);
  if (err != ERR_OK) {
    Serial.println("출차 시간이 입차 시간보다 빠르거나 잘못되었습니다.");
    show_temporary_lcd("시간오류!", "", 2000);
    return ERR_TIME_INVALID;
  }

  noInterrupts();
  s_parking_slots[oldest_idx].occupied = false;
  s_parking_slots[oldest_idx].fee = fee;
  interrupts();

  Serial.print("차량 #");
  Serial.print(oldest_idx + 1);
  Serial.printf(" 출차 %02d:%02d 요금: %d원\n", hour, minute, fee);

  char line1[LCD_LINE_LENGTH + 1U];
  char line2[LCD_LINE_LENGTH + 1U];
  int n1 = snprintf(line1, sizeof(line1), "출차됨 #%d", oldest_idx + 1);
  int n2 = snprintf(line2, sizeof(line2), "요금: %d 원", fee);
  if (n1 <= 0 || (size_t)n1 >= sizeof(line1)) {
    strncpy(line1, "출차됨", sizeof(line1));
    line1[sizeof(line1) - 1] = '\0';
  }
  if (n2 <= 0 || (size_t)n2 >= sizeof(line2)) {
    strncpy(line2, "요금: ?? 원", sizeof(line2));
    line2[sizeof(line2) - 1] = '\0';
  }

  s_lcd.clear();
  s_lcd.setCursor(0, 0);
  s_lcd.print(line1);
  s_lcd.setCursor(0, 1);
  s_lcd.print(line2);

  delay(3000);
  print_remaining_slots();

  return ERR_OK;
}

/**
 * @brief Prints command format error message to Serial based on given command.
 * @param[in] cmd Command character ('I' or 'O').
 */
static void print_command_error(char cmd)
{
  if (cmd == 'I' || cmd == 'i') {
    Serial.println("입력 형식 오류. I HH MM");
  } else {
    Serial.println("입력 형식 오류. O HH MM");
  }
}

/**
 * @brief Processes a single input command line.
 * @param[in] line Input command string line.
 */
static void process_line(const char *line)
{
  if (line == NULL || line[0] == '\0') {
    return;
  }

  char cmd = line[0];
  if (!(cmd == 'I' || cmd == 'i' || cmd == 'O' || cmd == 'o')) {
    Serial.println("명령어 오류. I 또는 O 입력 후 시간 입력");
    return;
  }

  int hour = -1;
  int minute = -1;
  if (!parse_time(line, &hour, &minute)) {
    print_command_error(cmd);
    return;
  }

  if (hour < MIN_HOUR || hour > MAX_HOUR || minute < MIN_MINUTE || minute > MAX_MINUTE) {
    print_command_error(cmd);
    return;
  }

  ErrorCode err;
  if (cmd == 'I' || cmd == 'i') {
    err = process_entry(hour, minute);
    (void)err; /* For potential future error handling */
  } else {
    err = process_exit(hour, minute);
    (void)err; /* For potential future error handling */
  }
}

/**
 * @brief Trims leading and trailing whitespace from the string.
 * @param[in,out] str The string to trim.
 */
static void trim_whitespace(char *str)
{
  if (str == NULL) {
    return;
  }

  size_t len = strlen(str);
  size_t start = 0U;

  while (start < len && (str[start] == ' ' || str[start] == '\t')) {
    start++;
  }

  if (start > 0U) {
    size_t remaining = len - start;
    if (remaining > 0U) {
      memmove(str, str + start, remaining + 1U); /* include null terminator */
    } else {
      str[0] = '\0';
    }
    len = remaining;
  }

  while (len > 0U) {
    char c = str[len - 1];
    if (c == ' ' || c == '\t') {
      str[len - 1] = '\0';
      len--;
    } else {
      break;
    }
  }
}

/**
 * @brief Arduino setup function.
 * Initializes Serial port, LCD, and parking slots.
 */
void setup(void)
{
  Serial.begin(SERIAL_BAUD_RATE);
  s_lcd.init();
  s_lcd.backlight();

  noInterrupts();
  for (uint32_t i = 0U; i < MAX_CARS; i++) {
    s_parking_slots[i].occupied = false;
    s_parking_slots[i].in_hour = 0;
    s_parking_slots[i].in_minute = 0;
    s_parking_slots[i].fee = 0;
  }
  interrupts();

  print_remaining_slots();
  Serial.println("주차장 출입 시스템 시작");
  Serial.println("입차: I HH MM");
  Serial.println("출차: O HH MM");
  Serial.println("예) 입차 09시30분 -> I 9 30");
  Serial.println("예) 출차 12시15분 -> O 12 15");
}

/**
 * @brief Arduino main loop function.
 * Reads serial input line by line and processes commands.
 */
void loop(void)
{
  static char input_line[MAX_INPUT_LENGTH + 1U] = {0};

  if (Serial.available() != 0) {
    size_t idx = 0U;
    while (Serial.available() != 0 && idx < MAX_INPUT_LENGTH) {
      int c_int = Serial.read();
      if (c_int == -1) {
        break;
      }
      char c = (char)c_int;
      if (c == '\r') {
        /* Ignore carriage return */
      } else if (c == '\n') {
        break;
      } else {
        input_line[idx++] = c;
      }
    }
    input_line[idx] = '\0';

    trim_whitespace(input_line);

    if (input_line[0] != '\0') {
      process_line(input_line);
    }
  }
}