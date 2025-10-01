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
    char display[17];
    double first;
    double second;
    Operator op;
    CalcState state;
} Calculator;

Calculator calc;

LCD_Handle_t lcd;

void lcd_init_custom(void) {
    lcd = lcd_create(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
    lcd_init(lcd, 16, 2);
    lcd_clear(lcd);
}

void lcd_display_line(const char* str, uint8_t line) {
    lcd_set_cursor(lcd, 0, line);
    lcd_print(lcd, str);
}

// 간단한 버튼 디바운스 함수 (예제용)
bool button_pressed(gpio_num_t pin) {
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

// 숫자 키 버튼 입력 (예제, 실제 여기에 번호 키 여러개를 구성해야 함)
int get_digit_from_button(void) {
    // 예) 버튼 분할 입력식 필요, 아래는 임시 1~9까지 순차입력 테스트용
    // 실제 키패드 / 버튼 매핑 또는 입력 방식에 맞게 변경 필요
    for (int i=0; i<=9; i++) {
        // 예: 버튼 여러개가 아니라면 다른 방법으로 숫자 입력 구현하세요.
        // 여기서는 시뮬레이션용으로 그냥 0~9 리턴.
    }
    return -1; // 입력 없음
}

// 단순 문자열->double 변환 (실시간 입력용)
double str_to_double(const char *str) {
    return atof(str);
}

// 계산 수행 함수
double calculate(double a, double b, Operator op) {
    switch(op) {
        case OP_ADD: return a + b;
        case OP_SUB: return a - b;
        case OP_MUL: return a * b;
        case OP_DIV: return b == 0 ? 0 : a / b;
        case OP_POW: return pow(a,b);
        default: return 0;
    }
}

// 공학용 함수 단일 입력
double calculate_single(double a, Operator op) {
    switch(op) {
        case OP_SIN: return sin(a);
        case OP_COS: return cos(a);
        case OP_TAN: return tan(a);
        case OP_LOG: return (a <= 0) ? 0 : log10(a);
        case OP_EXP: return exp(a);
        default: return 0;
    }
}

void reset_calc(void) {
    calc.first = 0;
    calc.second = 0;
    calc.op = OP_NONE;
    calc.state = INPUT_FIRST;
    strcpy(calc.display, "0");
}

// display에 숫자 입력 로직(문자 추가)
void append_digit(char d) {
    size_t len = strlen(calc.display);
    if (len < 16 && !(len ==1 && calc.display[0] =='0')) {
        calc.display[len] = d;
        calc.display[len+1] = '\0';
    } else if(len ==1 && calc.display[0] =='0') {
        calc.display[0] = d;
        calc.display[1] = '\0';
    }
}


// 버튼에 따른 동작 결정 (실제 입력 환경에 따라 다르게 구현 필요)
Operator get_operator_from_button(void) {
    // 임시: OP_ADD 반환 등 테스트용
    // 실제 각 연산 버튼의 gpio 읽고 반환하도록 수정 필요
    return OP_NONE;
}

void app_main(void) {
    ESP_LOGI(TAG, "Calculator Start");

    // LCD 초기화 (실제 lcd 라이브러리/핀으로 변경할 것)
    lcd_init_custom();

    // 버튼 GPIO 초기화 (보드 핀번호에 따른 설정)
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BUTTON_PIN_NUM) | (1ULL << BUTTON_PIN_OP) | (1ULL << BUTTON_PIN_EQ) | (1ULL << BUTTON_PIN_CLR);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1; // 풀업 사용
    gpio_config(&io_conf);

    reset_calc();
    lcd_display_line(calc.display, 0);
    lcd_display_line("", 1);

    while(1) {
        // 숫자 입력 버튼 처리 (여기선 더미로 0~9를 키보드 입력 흉내냄)
        if (button_pressed(BUTTON_PIN_NUM)) {
            // 임시 0~9 숫자 전환 예제
            static int digit = 0;
            char c = '0' + digit;
            digit = (digit +1) %10;

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
            vTaskDelay(pdMS_TO_TICKS(300)); // 버튼 재입력 딜레이
        }

        // 연산자 버튼 처리 (임시 예)
        if (button_pressed(BUTTON_PIN_OP)) {
            if (calc.state == INPUT_FIRST) {
                calc.op = OP_ADD; // 임시 덧셈으로 고정, 실제 버튼별 operator 구분 필요
                calc.state = INPUT_SECOND;
                strcpy(calc.display, "0");
                lcd_display_line("+", 1);
                lcd_display_line(calc.display, 0);
            } else if (calc.state == INPUT_SECOND) {
                // 연산 우선순위 무시, 중복 입력 처리 간단화
                // 실제 구현시 수정 권장
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
                strcpy(calc.display, buf);

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