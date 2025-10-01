#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lcd1602_i2c.h"  // Assume there is an LCD 16x2 I2C library available

// Define GPIO pins for buttons (for simplicity, assume 4 floors, buttons at these GPIOs)
#define BUTTON_FLOOR_1 GPIO_NUM_12
#define BUTTON_FLOOR_2 GPIO_NUM_14
#define BUTTON_FLOOR_3 GPIO_NUM_27
#define BUTTON_FLOOR_4 GPIO_NUM_33

// Elevator count and floor count
#define ELEVATOR_COUNT 2
#define FLOOR_COUNT 4

typedef struct {
    int current_floor;
    int target_floor;
    int moving; // 0 = idle, 1 = moving
    int direction; // -1 down, 0 idle, 1 up
} elevator_t;

static elevator_t elevators[ELEVATOR_COUNT];

// Queue to send requested floors
static xQueueHandle floor_queue = NULL;

// LCD handle
static lcd1602_t lcd;

// Utility: move elevator one floor step towards target
static void move_elevator_one_step(elevator_t *e) {
    if (e->moving) {
        if (e->current_floor < e->target_floor) {
            e->current_floor++;
            e->direction = 1;
        } else if (e->current_floor > e->target_floor) {
            e->current_floor--;
            e->direction = -1;
        }

        if (e->current_floor == e->target_floor) {
            e->moving = 0;
            e->direction = 0;
        }
    }
}

// Find closest elevator to floor request
static int find_closest_elevator(int floor) {
    int best = -1;
    int best_dist = 1000;
    for (int i=0; i<ELEVATOR_COUNT; i++) {
        if (elevators[i].moving && ((floor - elevators[i].current_floor)*(floor - elevators[i].current_floor)) > 10000){
          // Just an example to ignore far busy elevators (not necessary)
        }
        int dist = abs(elevators[i].current_floor - floor);
        if (best == -1 || dist < best_dist || (!elevators[i].moving && dist == best_dist)) {
            best = i;
            best_dist = dist;
        }
    }
    return best;
}

// Update LCD display of elevator states
static void lcd_update() {
    char line1[17];
    char line2[17];

    // Format: E1:F1 M0  E2:F2 M1  (Elevator# CurrentFloor Moving)
    // M: 0=idle,1=moving
    snprintf(line1, 17, "E1:F%d M%d   E2:F%d M%d", 
        elevators[0].current_floor, elevators[0].moving,
        elevators[1].current_floor, elevators[1].moving);

    // Show target floors or "Idle"
    char* status0 = elevators[0].moving ? "Moving" : "Idle  ";
    char* status1 = elevators[1].moving ? "Moving" : "Idle  ";

    snprintf(line2, 17, "Tgt:%d %s %d %s",
        elevators[0].moving ? elevators[0].target_floor : 0, status0,
        elevators[1].moving ? elevators[1].target_floor : 0, status1);

    lcd1602_clear(&lcd);
    lcd1602_puts(&lcd, line1);
    lcd1602_set_cursor(&lcd, 1, 0);
    lcd1602_puts(&lcd, line2);
}

// Elevator task
static void elevator_task(void *arg) {
    const TickType_t delay = pdMS_TO_TICKS(1000); // simulate 1 floor per sec movement

    while (1) {
        for (int i=0; i<ELEVATOR_COUNT; i++) {
            move_elevator_one_step(&elevators[i]);
        }
        lcd_update();
        vTaskDelay(delay);
    }
}

// Button ISR handler (simple edge detect)
static xQueueHandle gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

// Handle button presses and assign to elevators
static void button_task(void *arg) {
    uint32_t io_num;
    while (1) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            int floor = 0;
            switch(io_num) {
                case BUTTON_FLOOR_1: floor = 1; break;
                case BUTTON_FLOOR_2: floor = 2; break;
                case BUTTON_FLOOR_3: floor = 3; break;
                case BUTTON_FLOOR_4: floor = 4; break;
                default: floor = 0; break;
            }
            if (floor > 0) {
                int elv = find_closest_elevator(floor);
                if (elv >= 0) {
                    // Assign target to elevator if idle or closer
                    if (!elevators[elv].moving) {
                        elevators[elv].target_floor = floor;
                        elevators[elv].moving = 1;
                        elevators[elv].direction = (elevators[elv].current_floor < floor) ? 1 : -1;
                    }
                    // If already moving, do not override target (very simple logic)
                }
            }
        }
    }
}

void app_main(void) {
    // Init elevators at floor 1
    for (int i=0; i<ELEVATOR_COUNT; i++) {
        elevators[i].current_floor = 1;
        elevators[i].target_floor = 1;
        elevators[i].moving = 0;
        elevators[i].direction = 0;
    }

    // Init LCD (I2C pins for xiao-esp32-s3 assumed)
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_21,
        .scl_io_num = GPIO_NUM_22,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000
    };
    i2c_param_config(I2C_NUM_0, &i2c_conf);
    i2c_driver_install(I2C_NUM_0, i2c_conf.mode, 0, 0, 0);

    lcd1602_init(&lcd, I2C_NUM_0, 0x27);

    // Initialize buttons
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_FLOOR_1) | (1ULL << BUTTON_FLOOR_2) | (1ULL << BUTTON_FLOOR_3) | (1ULL << BUTTON_FLOOR_4),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_FLOOR_1, gpio_isr_handler, (void*) BUTTON_FLOOR_1);
    gpio_isr_handler_add(BUTTON_FLOOR_2, gpio_isr_handler, (void*) BUTTON_FLOOR_2);
    gpio_isr_handler_add(BUTTON_FLOOR_3, gpio_isr_handler, (void*) BUTTON_FLOOR_3);
    gpio_isr_handler_add(BUTTON_FLOOR_4, gpio_isr_handler, (void*) BUTTON_FLOOR_4);

    // Create elevator and button tasks
    xTaskCreate(elevator_task, "elevator_task", 2048, NULL, 5, NULL);
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    // Initial LCD display
    lcd_update();
}