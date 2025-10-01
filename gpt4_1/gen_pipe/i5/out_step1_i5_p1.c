#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

// LCD 설정 (I2C 주소 0x27, 16 chars, 2 lines)
static LiquidCrystal_I2C lcd(0x27, 16, 2);

#define MAX_CARS 10
#define PARKING_FEE_PER_HOUR 1000  // 예: 1시간당 1000원

typedef struct {
    bool occupied;
    int in_hour;
    int in_minute;
    int fee;
} ParkingSlot;

static ParkingSlot parkingSlots[MAX_CARS];

/**
 * @brief Count number of parked cars.
 * Stack usage: minimal (local int count)
 */
static int parkedCarsCount(void) {
    int count = 0;
    for (int i = 0; i < MAX_CARS; i++) {
        if (parkingSlots[i].occupied) count++;
    }
    return count;
}

/**
 * @brief Calculate parking fee based on in/out time.
 * @param in_h 입차 시간(시)
 * @param in_m 입차 시간(분)
 * @param out_h 출차 시간(시)
 * @param out_m 출차 시간(분)
 * @return 요금 or -1 if invalid time
 * Stack usage: minimal (local ints)
 */
static int calculateFee(int in_h, int in_m, int out_h, int out_m) {
    int in_total = in_h * 60 + in_m;
    int out_total = out_h * 60 + out_m;
    if (out_total < in_total) return -1; // 시간 오류
    int diff = out_total - in_total;
    int hours = diff / 60;
    if (diff % 60 != 0) hours++; // 초과분 1시간 추가
    return hours * PARKING_FEE_PER_HOUR;
}

/**
 * @brief Print parking status on LCD.
 * Stack usage: minimal
 */
static void printStatus(void) {
    lcd.clear();
    lcd.setCursor(0, 0);
    // 안전한 문자열 처리를 위해 고정 길이 문자열만 사용
    char line1[17];
    int remaining = MAX_CARS - parkedCarsCount();
    if (remaining < 0) remaining = 0;
    snprintf(line1, sizeof(line1), "남은 주차: %2d 대", remaining);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print("입력: I, 출입: O");
}

/**
 * @brief Safe parsing of input command line. 
 * 최대 크기 32바이트로 제한하고 null-종료 확인.
 * Stack usage: 최대 33바이트 배열 + 로컬 변수 약간
 */
static void processLine(const char *line) {
    // 최소 길이 검사
    if (line == NULL || line[0] == '\0') return;

    // 명령 문자 확인
    char cmd = line[0];
    if (cmd != 'I' && cmd != 'i' && cmd != 'O' && cmd != 'o') {
        Serial.println("명령어 오류. I 또는 O 입력 후 시간 입력");
        return;
    }

    int hour = -1, minute = -1;
    // sscanf 대신 직접 parsing 하거나 sscanf 쓰되 입력 길이 제한을 보장함
    // sscanf 안전 사용: line이 null 종료 문자열임을 가정
    if (sscanf(line, "%*c %d %d", &hour, &minute) != 2 
            || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        if (cmd == 'I' || cmd == 'i') {
            Serial.println("입력 형식 오류. I HH MM");
        } else {
            Serial.println("입력 형식 오류. O HH MM");
        }
        return;
    }

    if (cmd == 'I' || cmd == 'i') {
        if (parkedCarsCount() >= MAX_CARS) {
            Serial.println("만차입니다. 입차 불가.");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("만차입니다!");
            delay(2000);
            printStatus();
            return;
        }
        for (int i = 0; i < MAX_CARS; i++) {
            if (!parkingSlots[i].occupied) {
                parkingSlots[i].occupied = true;
                parkingSlots[i].in_hour = hour;
                parkingSlots[i].in_minute = minute;
                parkingSlots[i].fee = 0;
                Serial.printf("차량 #%d 입차 %02d:%02d\n", i + 1, hour, minute);
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("차량 입차됨");
                lcd.setCursor(0, 1);
                int remain = MAX_CARS - parkedCarsCount();
                if (remain < 0) remain = 0;
                char line1[17];
                snprintf(line1, sizeof(line1), "남은: %2d 대", remain);
                lcd.print(line1);
                delay(2000);
                printStatus();
                break;
            }
        }
    } else { // 출차
        if (parkedCarsCount() == 0) {
            Serial.println("주차된 차량이 없습니다.");
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("주차 차량 없음");
            delay(2000);
            printStatus();
            return;
        }
        int oldestIdx = -1;
        int oldestTime = 24 * 60 + 1;

        for (int i = 0; i < MAX_CARS; i++) {
            if (parkingSlots[i].occupied) {
                int inTime = parkingSlots[i].in_hour * 60 + parkingSlots[i].in_minute;
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

        int fee = calculateFee(parkingSlots[oldestIdx].in_hour, parkingSlots[oldestIdx].in_minute, hour, minute);
        if (fee < 0) {
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
        Serial.printf("차량 #%d 출차 %02d:%02d 요금: %d원\n", oldestIdx + 1, hour, minute, fee);
        lcd.clear();
        lcd.setCursor(0, 0);
        char line0[17];
        snprintf(line0, sizeof(line0), "출차됨 #%d", oldestIdx + 1);
        lcd.print(line0);
        lcd.setCursor(0, 1);
        char line1[17];
        snprintf(line1, sizeof(line1), "요금: %d 원", fee);
        lcd.print(line1);
        delay(3000);
        printStatus();
    }
}

void setup() {
    Serial.begin(115200);
    lcd.init();
    lcd.backlight();

    for (int i = 0; i < MAX_CARS; i++) {
        parkingSlots[i].occupied = false;
    }
    printStatus();
    Serial.println("주차장 출입 시스템 시작");
    Serial.println("입차: I HH MM");
    Serial.println("출차: O HH MM");
    Serial.println("예) 입차 09시30분 -> I 9 30");
    Serial.println("예) 출차 12시15분 -> O 12 15");
}

/**
 * @brief Main loop for command processing
 * Stack usage:
 * - line buffer 33 bytes on stack
 * - local ints and variables minimal
 */
void loop() {
    static char inputLine[33]; // 최대 32문자 + null 종료

    if (Serial.available()) {
        size_t idx = 0;
        while (Serial.available() && idx < sizeof(inputLine) - 1) {
            char c = Serial.read();
            if (c == '\r') continue; // CR 무시
            if (c == '\n') break;    // 개행에서 중단
            inputLine[idx++] = c;
        }
        inputLine[idx] = '\0';

        // 앞뒤 공백 제거
        char *startPtr = inputLine;
        while (*startPtr == ' ' || *startPtr == '\t') startPtr++;
        // 뒤 공백 자르기
        for (int i = (int)strlen(startPtr) - 1; i >= 0; i--) {
            if (startPtr[i] == ' ' || startPtr[i] == '\t')
                ((char *)startPtr)[i] = '\0';
            else
                break;
        }

        if (strlen(startPtr) > 0) {
            processLine(startPtr);
        }
    }
}