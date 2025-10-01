#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

// LCD 설정 (I2C 주소 0x27, 16 chars, 2 lines)
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define MAX_CARS 10
#define PARKING_FEE_PER_HOUR 1000  // 예: 1시간당 1000원

typedef struct {
    bool occupied;
    int in_hour;
    int in_minute;
    int out_hour;
    int out_minute;
    int fee;
} ParkingSlot;

ParkingSlot parkingSlots[MAX_CARS];

int parkedCarsCount() {
    int count = 0;
    for (int i = 0; i < MAX_CARS; i++) {
        if (parkingSlots[i].occupied) count++;
    }
    return count;
}

int calculateFee(int in_h, int in_m, int out_h, int out_m) {
    int in_total = in_h * 60 + in_m;
    int out_total = out_h * 60 + out_m;
    if (out_total < in_total) return -1; // 잘못된 시간
    int diff = out_total - in_total;
    int hours = diff / 60;
    if (diff % 60 != 0) hours++; // 1분이라도 넘으면 1시간 추가 요금
    return hours * PARKING_FEE_PER_HOUR;
}

void printStatus() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.printf("남은 주차: %2d 대", MAX_CARS - parkedCarsCount());
    lcd.setCursor(0, 1);
    lcd.print("입력: I, 출입: O");
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

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();

        if (line.length() < 1) return;

        char cmd = line.charAt(0);
        if (cmd == 'I' || cmd == 'i') { // 입차 처리
            int hour, minute;
            int ret = sscanf(line.c_str(), "%*c %d %d", &hour, &minute);
            if (ret != 2 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
                Serial.println("입력 형식 오류. I HH MM");
                return;
            }

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
                    lcd.printf("남은: %2d 대", MAX_CARS - parkedCarsCount());
                    delay(2000);
                    printStatus();
                    break;
                }
            }
        } 
        else if (cmd == 'O' || cmd == 'o') { // 출차 처리
            int hour, minute;
            int ret = sscanf(line.c_str(), "%*c %d %d", &hour, &minute);
            if (ret != 2 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
                Serial.println("입력 형식 오류. O HH MM");
                return;
            }

            if (parkedCarsCount() == 0) {
                Serial.println("주차된 차량이 없습니다.");
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("주차 차량 없음");
                delay(2000);
                printStatus();
                return;
            }

            // 출차할 차량 지정할 수 없으니 가장 먼저 입차된 차량 출차 (FIFO)
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
            lcd.printf("출차됨 #%d", oldestIdx + 1);
            lcd.setCursor(0, 1);
            lcd.printf("요금: %d 원", fee);
            delay(3000);
            printStatus();
        } 
        else {
            Serial.println("명령어 오류. I 또는 O 입력 후 시간 입력");
        }
    }
}