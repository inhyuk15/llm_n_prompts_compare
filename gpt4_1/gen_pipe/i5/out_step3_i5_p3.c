#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

#define MAX_CARS                (10U)
#define PARKING_FEE_PER_HOUR    (1000)
#define MAX_INPUT_LENGTH        (32U)
#define LCD_LINE_LENGTH         (16U)
#define INVALID_FEE             (-1)
#define MIN_HOUR                (0)
#define MAX_HOUR                (23)
#define MIN_MINUTE              (0)
#define MAX_MINUTE              (59)
#define MAX_TOTAL_MINUTES       (1440U)
#define SERIAL_BAUD_RATE        (115200U)

typedef bool boolean;

typedef struct {
    boolean occupied;
    int in_hour;
    int in_minute;
    int fee;
} ParkingSlot;

static LiquidCrystal_I2C lcd(0x27U, LCD_LINE_LENGTH, 2U);
static ParkingSlot parkingSlots[MAX_CARS] = {0};

static int32_t parkedCarsCount(void)
{
    int32_t count = 0;
    for (uint32_t i = 0U; i < MAX_CARS; i++) {
        if (parkingSlots[i].occupied) {
            count++;
        }
    }
    return count;
}

static int32_t calculateFee(int32_t in_h, int32_t in_m, int32_t out_h, int32_t out_m)
{
    uint32_t in_total = (uint32_t)in_h * 60U + (uint32_t)in_m;
    uint32_t out_total = (uint32_t)out_h * 60U + (uint32_t)out_m;

    if (out_total < in_total) {
        return INVALID_FEE;
    }
    uint32_t diff = out_total - in_total;
    uint32_t hours = diff / 60U;
    if ((diff % 60U) != 0U) {
        hours++;
    }
    return (int32_t)(hours * (uint32_t)PARKING_FEE_PER_HOUR);
}

static void printRemainingSlots(void)
{
    int32_t remaining = MAX_CARS - parkedCarsCount();
    if (remaining < 0) remaining = 0;
    char line[LCD_LINE_LENGTH + 1U];
    (void)snprintf(line, sizeof(line), "남은 주차: %2d 대", remaining);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line);
    lcd.setCursor(0, 1);
    lcd.print("입력: I, 출입: O");
}

static void showTemporaryLCD(const char *line1, const char *line2, uint32_t delayMs)
{
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(line2);
    delay(delayMs);
    printRemainingSlots();
}

static boolean parseTime(const char *line, int *hour, int *minute)
{
    if (sscanf(line, "%*c %d %d", hour, minute) != 2) {
        return false;
    }
    if (*hour < MIN_HOUR || *hour > MAX_HOUR || *minute < MIN_MINUTE || *minute > MAX_MINUTE) {
        return false;
    }
    return true;
}

static void processEntry(int hour, int minute)
{
    int32_t count = parkedCarsCount();
    if (count >= (int32_t)MAX_CARS) {
        Serial.println("만차입니다. 입차 불가.");
        showTemporaryLCD("만차입니다!", "", 2000);
        return;
    }
    for (int i = 0; i < (int32_t)MAX_CARS; i++) {
        if (!parkingSlots[i].occupied) {
            parkingSlots[i].occupied = true;
            parkingSlots[i].in_hour = hour;
            parkingSlots[i].in_minute = minute;
            parkingSlots[i].fee = 0;
            Serial.print("차량 #");
            Serial.print(i + 1);
            Serial.printf(" 입차 %02d:%02d\n", hour, minute);

            char line2[LCD_LINE_LENGTH + 1U];
            int32_t remain = MAX_CARS - parkedCarsCount();
            if (remain < 0) remain = 0;

            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("차량 입차됨");
            lcd.setCursor(0, 1);
            (void)snprintf(line2, sizeof(line2), "남은: %2d 대", remain);
            lcd.print(line2);

            delay(2000);
            printRemainingSlots();
            break;
        }
    }
}

static int findOldestParkedCarIndex(void)
{
    int32_t oldestIdx = -1;
    uint32_t oldestTime = MAX_TOTAL_MINUTES + 1U;
    for (int i = 0; i < (int32_t)MAX_CARS; i++) {
        if (parkingSlots[i].occupied) {
            uint32_t inTime = (uint32_t)parkingSlots[i].in_hour * 60U + (uint32_t)parkingSlots[i].in_minute;
            if (inTime < oldestTime) {
                oldestTime = inTime;
                oldestIdx = i;
            }
        }
    }
    return oldestIdx;
}

static void processExit(int hour, int minute)
{
    int32_t count = parkedCarsCount();
    if (count == 0) {
        Serial.println("주차된 차량이 없습니다.");
        showTemporaryLCD("주차 차량 없음", "", 2000);
        return;
    }
    int oldestIdx = findOldestParkedCarIndex();
    if (oldestIdx == -1) {
        Serial.println("출차할 차량이 없습니다.");
        showTemporaryLCD("출차 차량없음", "", 2000);
        return;
    }
    int fee = calculateFee(parkingSlots[oldestIdx].in_hour, parkingSlots[oldestIdx].in_minute, hour, minute);
    if (fee == INVALID_FEE) {
        Serial.println("출차 시간이 입차 시간보다 빠릅니다.");
        showTemporaryLCD("시간오류!", "", 2000);
        return;
    }
    parkingSlots[oldestIdx].occupied = false;
    parkingSlots[oldestIdx].fee = fee;

    Serial.print("차량 #");
    Serial.print(oldestIdx + 1);
    Serial.printf(" 출차 %02d:%02d 요금: %d원\n", hour, minute, fee);

    char line1[LCD_LINE_LENGTH + 1U];
    char line2[LCD_LINE_LENGTH + 1U];
    lcd.clear();
    lcd.setCursor(0, 0);
    (void)snprintf(line1, sizeof(line1), "출차됨 #%d", oldestIdx + 1);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    (void)snprintf(line2, sizeof(line2), "요금: %d 원", fee);
    lcd.print(line2);

    delay(3000);
    printRemainingSlots();
}

static void printCommandError(char cmd)
{
    if (cmd == 'I' || cmd == 'i') {
        Serial.println("입력 형식 오류. I HH MM");
    } else {
        Serial.println("입력 형식 오류. O HH MM");
    }
}

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
    if (cmd == 'I' || cmd == 'i') {
        processEntry(hour, minute);
    } else {
        processExit(hour, minute);
    }
}

static void trimWhitespace(char *str)
{
    if (str == NULL) return;
    // Trim leading whitespace
    while (*str == ' ' || *str == '\t') {
        memmove(str, str + 1, strlen(str));
    }
    // Trim trailing whitespace
    size_t len = strlen(str);
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

void setup(void)
{
    Serial.begin(SERIAL_BAUD_RATE);
    lcd.init();
    lcd.backlight();

    for (uint32_t i = 0U; i < MAX_CARS; i++) {
        parkingSlots[i].occupied = false;
    }

    printRemainingSlots();
    Serial.println("주차장 출입 시스템 시작");
    Serial.println("입차: I HH MM");
    Serial.println("출차: O HH MM");
    Serial.println("예) 입차 09시30분 -> I 9 30");
    Serial.println("예) 출차 12시15분 -> O 12 15");
}

void loop(void)
{
    static char inputLine[MAX_INPUT_LENGTH + 1U] = {0};

    if (Serial.available() != 0) {
        size_t idx = 0U;
        while (Serial.available() != 0 && idx < MAX_INPUT_LENGTH) {
            char c = (char)Serial.read();
            if (c == '\r') {
                // ignore
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