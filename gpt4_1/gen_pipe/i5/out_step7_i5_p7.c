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
    ERR_OK = 0,            /**< Operation successful */
    ERR_INVALID_ARGUMENT,  /**< Invalid argument error */
    ERR_PARKING_FULL,      /**< Parking lot is full */
    ERR_PARKING_EMPTY,     /**< Parking lot is empty */
    ERR_TIME_INVALID,      /**< Invalid time input */
    ERR_FEE_CALC,          /**< Fee calculation error */
    ERR_BUFFER_OVERFLOW,   /**< Buffer overflow error */
    ERR_NOT_FOUND,         /**< Requested item not found */
} ErrorCode;

/**
 * @struct ParkingSlot
 * @brief Represents a parking slot's status and timing information.
 */
typedef struct {
    volatile bool occupied;    /**< Status whether the slot is occupied */
    volatile int in_hour;      /**< Entry hour (0..23) */
    volatile int in_minute;    /**< Entry minute (0..59) */
    volatile int fee;          /**< Calculated parking fee */
} ParkingSlot;

static LiquidCrystal_I2C lcd(0x27U, LCD_LINE_LENGTH, 2U); /**< LCD display instance */
static volatile ParkingSlot parkingSlots[MAX_CARS] = {0}; /**< Parking slots array */

/**
 * @brief Counts the number of currently parked cars.
 * @param[out] count Pointer to store the counted number of parked cars.
 * @return ErrorCode ERR_OK on success, ERR_INVALID_ARGUMENT if count is NULL.
 */
static ErrorCode parkedCarsCount(int32_t *count)
{
    if (count == NULL) {
        return ERR_INVALID_ARGUMENT;
    }

    int32_t local_count = 0;
    noInterrupts();
    for (uint32_t i = 0U; i < MAX_CARS; i++) {
        if (parkingSlots[i].occupied) {
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
static ErrorCode calculateFee(int32_t in_h, int32_t in_m, int32_t out_h, int32_t out_m, int32_t *fee)
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
static void printRemainingSlots(void)
{
    int32_t remaining = 0;
    if (parkedCarsCount(&remaining) != ERR_OK) {
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
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line);
    lcd.setCursor(0, 1);
    lcd.print("입력: I, 출입: O");
}

/**
 * @brief Shows a temporary message on the LCD for specified delay.
 * @param[in] line1 First line string.
 * @param[in] line2 Second line string.
 * @param[in] delayMs Delay duration in milliseconds.
 */
static void showTemporaryLCD(const char *line1, const char *line2, uint32_t delayMs)
{
    if (line1 == NULL || line2 == NULL) {
        return;
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(line2);
    delay(delayMs);
    printRemainingSlots();
}

/**
 * @brief Parses time from input string in format "COMMAND HH MM".
 * @param[in] line Input string.
 * @param[out] hour Pointer to store parsed hour.
 * @param[out] minute Pointer to store parsed minute.
 * @return true if parsing succeeds and values are valid, false otherwise.
 */
static bool parseTime(const char *line, int *hour, int *minute)
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
static ErrorCode processEntry(int hour, int minute)
{
    if (hour < MIN_HOUR || hour > MAX_HOUR || minute < MIN_MINUTE || minute > MAX_MINUTE) {
        return ERR_INVALID_ARGUMENT;
    }

    noInterrupts();
    int32_t count = 0;
    ErrorCode err = parkedCarsCount(&count);
    if (err != ERR_OK || count >= (int32_t)MAX_CARS) {
        interrupts();
        Serial.println("만차입니다. 입차 불가.");
        showTemporaryLCD("만차입니다!", "", 2000);
        return ERR_PARKING_FULL;
    }

    for (int i = 0; i < (int32_t)MAX_CARS; i++) {
        if (!parkingSlots[i].occupied) {
            parkingSlots[i].occupied = true;
            parkingSlots[i].in_hour = hour;
            parkingSlots[i].in_minute = minute;
            parkingSlots[i].fee = 0;
            interrupts();

            Serial.print("차량 #");
            Serial.print(i + 1);
            Serial.printf(" 입차 %02d:%02d\n", hour, minute);

            if (parkedCarsCount(&count) != ERR_OK) {
                count = 0;
            }
            int32_t remain = (int32_t)MAX_CARS - count;
            if (remain < 0) remain = 0;
            else if (remain > (int32_t)MAX_CARS) remain = (int32_t)MAX_CARS;

            char line2[LCD_LINE_LENGTH + 1U];
            int n = snprintf(line2, sizeof(line2), "남은: %2d 대", remain);
            if (n <= 0 || (size_t)n >= sizeof(line2)) {
                strncpy(line2, "남은: ?? 대", sizeof(line2));
                line2[sizeof(line2) - 1] = '\0';
            }

            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("차량 입차됨");
            lcd.setCursor(0, 1);
            lcd.print(line2);

            delay(2000);
            printRemainingSlots();
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
static ErrorCode findOldestParkedCarIndex(int *index)
{
    if (index == NULL) {
        return ERR_INVALID_ARGUMENT;
    }

    int32_t oldestIdx = -1;
    uint32_t oldestTime = MAX_TOTAL_MINUTES + 1U;

    noInterrupts();
    for (int i = 0; (uint32_t)i < MAX_CARS; i++) {
        if (parkingSlots[i].occupied) {
            int ih = parkingSlots[i].in_hour;
            int im = parkingSlots[i].in_minute;
            if (ih < MIN_HOUR || ih > MAX_HOUR || im < MIN_MINUTE || im > MAX_MINUTE) {
                continue;
            }
            uint32_t inTime = (uint32_t)ih * 60U + (uint32_t)im;
            if (inTime < oldestTime) {
                oldestTime = inTime;
                oldestIdx = i;
            }
        }
    }
    interrupts();

    if (oldestIdx == -1) {
        return ERR_NOT_FOUND;
    }

    *index = oldestIdx;
    return ERR_OK;
}

/**
 * @brief Processes car exit with specified time, calculates fee, and updates slot.
 * @param[in] hour Exit hour (0..23).
 * @param[in] minute Exit minute (0..59).
 * @return ErrorCode indicating success or failure reason.
 */
static ErrorCode processExit(int hour, int minute)
{
    if (hour < MIN_HOUR || hour > MAX_HOUR || minute < MIN_MINUTE || minute > MAX_MINUTE) {
        return ERR_INVALID_ARGUMENT;
    }

    int32_t count = 0;
    if (parkedCarsCount(&count) != ERR_OK) {
        Serial.println("주차된 차량 데이터 오류.");
        showTemporaryLCD("데이터 오류", "", 2000);
        return ERR_PARKING_EMPTY;
    }
    if (count == 0) {
        Serial.println("주차된 차량이 없습니다.");
        showTemporaryLCD("주차 차량 없음", "", 2000);
        return ERR_PARKING_EMPTY;
    }

    int oldestIdx = -1;
    ErrorCode err = findOldestParkedCarIndex(&oldestIdx);
    if (err != ERR_OK || oldestIdx == -1) {
        Serial.println("출차할 차량이 없습니다.");
        showTemporaryLCD("출차 차량없음", "", 2000);
        return ERR_NOT_FOUND;
    }

    int slotInHour = parkingSlots[oldestIdx].in_hour;
    int slotInMinute = parkingSlots[oldestIdx].in_minute;

    int fee = 0;
    err = calculateFee(slotInHour, slotInMinute, hour, minute, &fee);
    if (err != ERR_OK) {
        Serial.println("출차 시간이 입차 시간보다 빠르거나 잘못되었습니다.");
        showTemporaryLCD("시간오류!", "", 2000);
        return ERR_TIME_INVALID;
    }

    noInterrupts();
    parkingSlots[oldestIdx].occupied = false;
    parkingSlots[oldestIdx].fee = fee;
    interrupts();

    Serial.print("차량 #");
    Serial.print(oldestIdx + 1);
    Serial.printf(" 출차 %02d:%02d 요금: %d원\n", hour, minute, fee);

    char line1[LCD_LINE_LENGTH + 1U];
    char line2[LCD_LINE_LENGTH + 1U];
    int n1 = snprintf(line1, sizeof(line1), "출차됨 #%d", oldestIdx + 1);
    int n2 = snprintf(line2, sizeof(line2), "요금: %d 원", fee);
    if (n1 <= 0 || (size_t)n1 >= sizeof(line1)) {
        strncpy(line1, "출차됨", sizeof(line1));
        line1[sizeof(line1) - 1] = '\0';
    }
    if (n2 <= 0 || (size_t)n2 >= sizeof(line2)) {
        strncpy(line2, "요금: ?? 원", sizeof(line2));
        line2[sizeof(line2) - 1] = '\0';
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(line2);

    delay(3000);
    printRemainingSlots();

    return ERR_OK;
}

/**
 * @brief Prints command format error message to Serial based on given command.
 * @param[in] cmd Command character ('I' or 'O').
 */
static void printCommandError(char cmd)
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
static void processLine(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }

    char cmd = line[0];
    if (!(cmd == 'I' || cmd == 'i' || cmd == 'O' || cmd == 'o')) {
        Serial.println("명령어 오류. I 또는 O 입력 후 시간 입력");
        return;
    }

    int hour = -1, minute = -1;
    if (!parseTime(line, &hour, &minute)) {
        printCommandError(cmd);
        return;
    }

    if (hour < MIN_HOUR || hour > MAX_HOUR || minute < MIN_MINUTE || minute > MAX_MINUTE) {
        printCommandError(cmd);
        return;
    }

    ErrorCode err;
    if (cmd == 'I' || cmd == 'i') {
        err = processEntry(hour, minute);
        (void)err; /* For potential future error handling */
    } else {
        err = processExit(hour, minute);
        (void)err; /* For potential future error handling */
    }
}

/**
 * @brief Trims leading and trailing whitespace from the string.
 * @param[in,out] str The string to trim.
 */
static void trimWhitespace(char *str)
{
    if (str == NULL) {
        return;
    }

    size_t len = strlen(str);
    size_t start = 0;

    while (start < len && (str[start] == ' ' || str[start] == '\t')) {
        start++;
    }
    if (start > 0) {
        size_t remaining = len - start;
        if (remaining > 0) {
            memmove(str, str + start, remaining + 1U); /* include null terminator */
        } else {
            str[0] = '\0';
        }
        len = remaining;
    }

    while (len > 0) {
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
    lcd.init();
    lcd.backlight();

    noInterrupts();
    for (uint32_t i = 0U; i < MAX_CARS; i++) {
        parkingSlots[i].occupied = false;
        parkingSlots[i].in_hour = 0;
        parkingSlots[i].in_minute = 0;
        parkingSlots[i].fee = 0;
    }
    interrupts();

    printRemainingSlots();
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
    static char inputLine[MAX_INPUT_LENGTH + 1U] = {0};

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
                inputLine[idx++] = c;
            }
        }
        inputLine[idx] = '\0';

        trimWhitespace(inputLine);

        if (inputLine[0] != '\0') {
            processLine(inputLine);
        }
    }
}