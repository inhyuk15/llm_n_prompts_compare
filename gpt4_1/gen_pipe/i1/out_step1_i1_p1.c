#include <stdio.h>
#include <string.h>
#include <math.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// LCD 16x2 관련 헤더 (예제용, 실제 보드 환경에 맞게 변경 필요)
#include "lcd.h"

#define BUTTON_PIN_NUM     0  // 버튼 신호 입력 핀 예, 실제 핀번호 보드에 맞게 변경
#define BUTTON_PIN_OP      1  // 연산자 입력 버튼 핀
#define BUTTON_PIN_EQ      2  // '=' 버튼 핀
#define BUTTON_PIN_CLR     3  // 'Clear' 버튼 핀

// LCD 핀, 포트 예제 (보드 핀에 맞게 조정)
#define LCD_RS  5
#define LCD_EN  6
#define LCD_D4  7
#define LCD_D5  8
#define LCD_D6  9
#define LCD_D7  10

static const char *TAG = "Calc";

// 계산 상태
typedef enum {
    INPUT_FIRST,
    INPUT_OPERATOR,
    INPUT_SECOND,
    SHOW_RESULT
} CalcState;

typedef enum {
    OP_NONE = 0,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_SIN,
    OP_COS,
    OP_TAN,
    OP_LOG,
    OP_EXP,
    OP_POW
} Operator;

typedef struct {
    char display[17]; // 16 chars + null-terminator
    double first;
    double second;
    Operator op;
    CalcState state;
} Calculator;

// 정적으로 할당된 전역변수로 함수 스코프 제한 위해 static 사용
static Calculator calc;

/* 정적 LCD 핸들 */
static LCD_Handle_t lcd;

/**
 * @brief 스택 사용량: lcd_create, lcd_init, lcd_clear 내부에서 사용하는 스택 제외, 
 *                   이 함수 내 로컬변수 없음, 즉 거의 0바이트
*/
static void lcd_init_custom(void) {
    lcd = lcd_create(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
    lcd_init(lcd, 16, 2);
    lcd_clear(lcd);
}

/**
 * @brief 스택 사용량: lcd_set_cursor, lcd_print 내부 스택량 제외,
 *                   함수 내 로컬변수 없음, 0바이트
 *
 * @param str 출력할 문자열, 최대 16바이트 이상 보장 필요
 * @param line 0 또는 1 (LCD 16x2)
 */
static void lcd_display_line(const char* str, uint8_t line) {
    lcd_set_cursor(lcd, 0, line);
    lcd_print(lcd, str);
}

/**
 * @brief 스택 사용량: 内部 vTaskDelay 호출 및 간단 비교, 로컬변수 없음 0바이트
 *
 * @param pin gpio_num_t 버튼 핀 번호
 * @return true 버튼 눌림 감지 (디바운스 적용됨)
 * @return false 눌리지 않음
 */
static bool button_pressed(gpio_num_t pin) {
    if (gpio_get_level(pin) == 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (gpio_get_level(pin) == 0) {
            while (gpio_get_level(pin) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            return true;
        }
    }
    return false;
}

/* 
 * 아래 함수 get_digit_from_button 은 미구현된 상태로 본 예제에선
 * BUTTON_PIN_NUM 단일 버튼으로 0~9 순환 입력을 하도록 app_main 구현에서 처리함.
 * 따라서 삭제하거나 그대로 두되 호출하지 않도록 한다.
 */

/**
 * @brief 문자열을 double로 변환 (atof 내부 사용)
 * 스택 사용량 = atof 내부 사용량 (단순)
 *
 * @param str 숫자 문자열
 * @return double 변환값
 */
static double str_to_double(const char *str) {
    return atof(str);
}

/**
 * @brief 두 개 숫자와 연산자로 결과 계산
 *
 * 스택 사용량: pow 함수 호출 시 약간 증가 (내부 구현에 따름), 
 *             로컬 변수 없음
 */
static double calculate(double a, double b, Operator op) {
    switch(op) {
        case OP_ADD: return a + b;
        case OP_SUB: return a - b;
        case OP_MUL: return a * b;
        case OP_DIV: return b == 0 ? 0 : a / b;
        case OP_POW: return pow(a,b);
        default: return 0;
    }
}

/**
 * @brief 단일 입력 공학용 함수 계산
 * 
 * @param a 입력값
 * @param op 연산자
 * @return double 결과
 * 
 * 스택 사용량: 삼각함수 및 로그, 지수 함수 내부 사용량 있음.
 */
static double calculate_single(double a, Operator op) {
    switch(op) {
        case OP_SIN: return sin(a);
        case OP_COS: return cos(a);
        case OP_TAN: return tan(a);
        case OP_LOG: return (a <= 0) ? 0 : log10(a);
        case OP_EXP: return exp(a);
        default: return 0;
    }
}

/**
 * @brief 계산기 초기화
 * 
 * 스택 사용량: 로컬변수 없음, strcpy 내부 메모리 접근 최소
 */
static void reset_calc(void) {
    calc.first = 0;
    calc.second = 0;
    calc.op = OP_NONE;
    calc.state = INPUT_FIRST;
    // 0으로 초기화 display string, 널 종료 포함
    strcpy(calc.display, "0");
}

/**
 * @brief display 문자열에 숫자 추가
 *  
 * 스택 사용량: strlen, 로컬변수 size_t len 8바이트 (64bit 환경 기준) = 8바이트
 * buffer overflow 방지 경계 체크 포함
 *
 * @param d 추가할 숫자 문자 ('0'~'9')
 */
static void append_digit(char d) {
    size_t len = strlen(calc.display);
    if (len < 16) {
        // 첫글자가 0이면 교체
        if(len == 1 && calc.display[0] == '0') {
            calc.display[0] = d;
            calc.display[1] = '\0';
        } else {
            calc.display[len] = d;
            calc.display[len+1] = '\0';
        }
    }
}

/**
 * @brief 버튼 입력에 따른 연산자 결정 (임시: 고정 OP_ADD)
 * 스택 사용량: 없음
 *
 * @return Operator 현재 임시 OP_NONE 대신 OP_ADD 반환
 */
static Operator get_operator_from_button(void) {
    // 임시: 실제 GPIO 판독 후 반환하도록 구현해야 함
    return OP_NONE;
}

/**
 * @brief 계산기 메인 태스크 함수
 *
 * 스택 사용량: 
 * - 지역변수: 
 *   char c (1byte) 
 *   static int digit (4 or 8바이트, 올라간다 해도 static은 데이터영역, 스택아님)
 *   char buf[17] (17 bytes)
 * 총 약 18바이트 + 내부 lcd 함수, snprintf 호출 스택 내부량 별도.
 */
void app_main(void) {
    ESP_LOGI(TAG, "Calculator Start");

    // LCD 초기화
    lcd_init_custom();

    // 버튼 GPIO 초기화
    static gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_PIN_NUM) | (1ULL << BUTTON_PIN_OP) | (1ULL << BUTTON_PIN_EQ) | (1ULL << BUTTON_PIN_CLR),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf);

    reset_calc();
    lcd_display_line(calc.display, 0);
    lcd_display_line("", 1);

    static int digit = 0;  // 영속 데이터로 static 선언 (스택 아님)

    while(1) {
        char c;

        // 숫자 입력 단일 버튼 처리 (0~9 순환)
        if (button_pressed(BUTTON_PIN_NUM)) {
            c = (char)('0' + digit);
            digit = (digit + 1) % 10;

            if (calc.state == INPUT_FIRST || calc.state == SHOW_RESULT) {
                if (calc.state == SHOW_RESULT) {
                    reset_calc();
                }
                append_digit(c);
                calc.first = str_to_double(calc.display);
                lcd_display_line(calc.display, 0);
            } else if (calc.state == INPUT_SECOND) {
                append_digit(c);
                calc.second = str_to_double(calc.display);
                lcd_display_line(calc.display, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        // 연산자 버튼 처리 (임시: OP_ADD 고정)
        if (button_pressed(BUTTON_PIN_OP)) {
            if (calc.state == INPUT_FIRST) {
                calc.op = OP_ADD; // 임시: 실제 버튼에 맞게 변경해야 함
                calc.state = INPUT_SECOND;
                strcpy(calc.display, "0");
                lcd_display_line("+", 1);
                lcd_display_line(calc.display, 0);
            } else if (calc.state == INPUT_SECOND) {
                // 중복 입력 처리 간략화를 위해 임시 처리
                double res = calculate(calc.first, calc.second, calc.op);
                calc.first = res;
                calc.op = OP_ADD; // 임시
                calc.second = 0;
                calc.state = INPUT_SECOND;
                strcpy(calc.display, "0");
                char buf[17];
                snprintf(buf, sizeof(buf), "=%.8g", res);
                lcd_display_line(buf, 1);
                lcd_display_line(calc.display, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        // '=' 버튼 처리
        if (button_pressed(BUTTON_PIN_EQ)) {
            if (calc.state == INPUT_SECOND) {
                double res = 0;
                if (calc.op == OP_NONE) {
                    res = calc.first;
                } else if (calc.op == OP_SIN || calc.op == OP_COS || calc.op == OP_TAN ||
                           calc.op == OP_LOG || calc.op == OP_EXP) {
                    res = calculate_single(calc.first, calc.op);
                } else {
                    res = calculate(calc.first, calc.second, calc.op);
                }
                calc.first = res;
                calc.second = 0;
                calc.state = SHOW_RESULT;

                char buf[17];
                snprintf(buf, sizeof(buf), "%.8g", res);
                // strncpy로 복사하되 null 보장
                // strcpy보다 더 안전하지만 buf 크기가 17으로 동일해 사실상 동일
                strncpy(calc.display, buf, sizeof(calc.display)-1);
                calc.display[sizeof(calc.display)-1] = '\0';

                lcd_display_line(calc.display, 0);
                lcd_display_line("Result", 1);
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        // Clear 버튼 처리
        if (button_pressed(BUTTON_PIN_CLR)) {
            reset_calc();
            lcd_display_line(calc.display, 0);
            lcd_display_line("", 1);
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}