#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

#define MAX_CARS                (10U)
#define PARKING_FEE_PER_HOUR    (1000)    /* 예: 1시간당 1000원 */
#define MAX_INPUT_LENGTH        (32U)
#define LCD_LINE_LENGTH         (16U)
#define INVALID_FEE             (-1)
#define MIN_HOUR                (0)
#define MAX_HOUR                (23)
#define MIN_MINUTE              (0)
#define MAX_MINUTE              (59)
#define MAX_TOTAL_MINUTES       (1440U)    /* 24 * 60 */
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
    uint32_t i;
    int32_t count = 0;

    for (i = 0U; i < MAX_CARS; i++) {
        if (parkingSlots[i].occupied != false) {
            count++;
        }
    }
    return count;
}

static int32_t calculateFee(int32_t in_h, int32_t in_m, int32_t out_h, int32_t out_m)
{
    int32_t retVal;
    uint32_t in_total;
    uint32_t out_total;
    uint32_t diff;
    uint32_t hours;

    /* Explicit conversion to unsigned to avoid sign comparison */
    in_total = (uint32_t)in_h * 60U + (uint32_t)in_m;
    out_total = (uint32_t)out_h * 60U + (uint32_t)out_m;

    if (out_total < in_total) {
        retVal = INVALID_FEE;
    } else {
        diff = out_total - in_total;
        hours = diff / 60U;
        if ((diff % 60U) != 0U) {
            hours++;
        }
        retVal = (int32_t)((uint32_t)hours * (uint32_t)PARKING_FEE_PER_HOUR);
    }
    return retVal;
}

static void printStatus(void)
{
    int32_t remaining;
    char line1[LCD_LINE_LENGTH + 1U];

    lcd.clear();
    lcd.setCursor(0, 0);

    remaining = MAX_CARS - parkedCarsCount();

    if (remaining < 0) {
        remaining = 0;
    }

    /* Use safe snprintf with fixed size buffer */
    (void)snprintf(line1, sizeof(line1), "남은 주차: %2d 대", remaining);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print("입력: I, 출입: O");
}

static void processLine(const char *line)
{
    char cmd;
    int32_t hour = -1;
    int32_t minute = -1;
    int32_t i;
    int32_t remain;
    int32_t count;
    int32_t oldestIdx;
    uint32_t oldestTime;
    int32_t fee;
    char lineBuffer[LCD_LINE_LENGTH + 1U];

    if ((line == NULL) || (line[0] == '\0')) {
        return;
    }

    cmd = line[0];

    /* Check command with explicit comparisons */
    if (((cmd != 'I') && (cmd != 'i')) && ((cmd != 'O') && (cmd != 'o'))) {
        Serial.println("명령어 오류. I 또는 O 입력 후 시간 입력");
        return;
    }

    if (sscanf(line, "%*c %d %d", &hour, &minute) != 2) {
        if ((cmd == 'I') || (cmd == 'i')) {
            Serial.println("입력 형식 오류. I HH MM");
        } else {
            Serial.println("입력 형식 오류. O HH MM");
        }
        return;
    }

    if ((hour < MIN_HOUR) || (hour > MAX_HOUR) || (minute < MIN_MINUTE) || (minute > MAX_MINUTE)) {
        if ((cmd == 'I') || (cmd == 'i')) {
            Serial.println("입력 형식 오류. I HH MM");
        } else {
            Serial.println("입력 형식 오류. O HH MM");
        }
        return;
    }

    count = parkedCarsCount();

    if ((cmd == 'I') || (cmd == 'i')) {
        if (count >= (int32_t)MAX_CARS) {
            Serial.println("만차입니다. 입차 불가.");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("만차입니다!");
            delay(2000);
            printStatus();
            return;
        }

        for (i = 0; i < (int32_t)MAX_CARS; i++) {
            if (parkingSlots[i].occupied == false) {
                parkingSlots[i].occupied = true;
                parkingSlots[i].in_hour = hour;
                parkingSlots[i].in_minute = minute;
                parkingSlots[i].fee = 0;
                Serial.print("차량 #");
                Serial.print(i + 1);
                Serial.printf(" 입차 %02d:%02d\n", hour, minute);
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("차량 입차됨");
                lcd.setCursor(0, 1);

                remain = MAX_CARS - parkedCarsCount();
                if (remain < 0) {
                    remain = 0;
                }
                (void)snprintf(lineBuffer, sizeof(lineBuffer), "남은: %2d 대", remain);
                lcd.print(lineBuffer);
                delay(2000);
                printStatus();
                break;
            }
        }
    } else {
        if (count == 0) {
            Serial.println("주차된 차량이 없습니다.");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("주차 차량 없음");
            delay(2000);
            printStatus();
            return;
        }

        oldestIdx = -1;
        oldestTime = MAX_TOTAL_MINUTES + 1U;

        for (i = 0; i < (int32_t)MAX_CARS; i++) {
            if (parkingSlots[i].occupied != false) {
                const uint32_t inTime = (uint32_t)parkingSlots[i].in_hour * 60U + (uint32_t)parkingSlots[i].in_minute;
                if (inTime < oldestTime) {
                    oldestTime = inTime;
                    oldestIdx = i;
                }
            }
        }

        if (oldestIdx == -1) {
            Serial.println("출차할 차량이 없습니다.");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("출차 차량없음");
            delay(2000);
            printStatus();
            return;
        }

        fee = calculateFee(parkingSlots[oldestIdx].in_hour, parkingSlots[oldestIdx].in_minute, hour, minute);
        if (fee == INVALID_FEE) {
            Serial.println("출차 시간이 입차 시간보다 빠릅니다.");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("시간오류!");
            delay(2000);
            printStatus();
            return;
        }

        parkingSlots[oldestIdx].occupied = false;
        parkingSlots[oldestIdx].fee = fee;

        Serial.print("차량 #");
        Serial.print(oldestIdx + 1);
        Serial.printf(" 출차 %02d:%02d 요금: %d원\n", hour, minute, fee);

        lcd.clear();
        lcd.setCursor(0,0);
        (void)snprintf(lineBuffer, sizeof(lineBuffer), "출차됨 #%d", oldestIdx + 1);
        lcd.print(lineBuffer);
        lcd.setCursor(0, 1);
        (void)snprintf(lineBuffer, sizeof(lineBuffer), "요금: %d 원", fee);
        lcd.print(lineBuffer);

        delay(3000);
        printStatus();
    }
}

void setup(void)
{
    uint32_t i;

    (void)Serial.begin(SERIAL_BAUD_RATE);
    lcd.init();
    lcd.backlight();

    for (i = 0U; i < MAX_CARS; i++) {
        parkingSlots[i].occupied = false;
    }

    printStatus();
    Serial.println("주차장 출입 시스템 시작");
    Serial.println("입차: I HH MM");
    Serial.println("출차: O HH MM");
    Serial.println("예) 입차 09시30분 -> I 9 30");
    Serial.println("예) 출차 12시15분 -> O 12 15");
}

void loop(void)
{
    static char inputLine[MAX_INPUT_LENGTH + 1U] = {0};
    size_t idx = 0U;
    int32_t length;
    char * startPtr;
    int32_t i;

    if (Serial.available() != 0) {
        idx = 0U;
        while ((Serial.available() != 0) && (idx < MAX_INPUT_LENGTH)) {
            const char c = (char)Serial.read();

            if (c == '\r') {
                /* Ignore carriage return */
            } else if (c == '\n') {
                break;
            } else {
                inputLine[idx] = c;
                idx++;
            }
        }
        inputLine[idx] = '\0';

        startPtr = inputLine;

        /* Trim leading whitespace: only ' ' and '\t' */
        while ((startPtr[0] == ' ') || (startPtr[0] == '\t')) {
            startPtr++;
        }

        /* Trim trailing whitespace */
        length = (int32_t)strlen(startPtr);
        for (i = length - 1; i >= 0; i--) {
            if ((startPtr[i] == ' ') || (startPtr[i] == '\t')) {
                /* Cast away const but guaranteed safe as inputLine is non-const */
                ((char *)startPtr)[i] = '\0';
            } else {
                break;
            }
        }

        if (startPtr[0] != '\0') {
            processLine(startPtr);
        }
    }
}