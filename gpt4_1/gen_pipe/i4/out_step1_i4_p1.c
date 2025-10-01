#include <stdio.h>
#include <string.h>
#include <stdlib.h> // for abs()
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lcd1602_i2c.h"

// Define GPIO pins for buttons (4 floors)
#define BUTTON_FLOOR_1 GPIO_NUM_12
#define BUTTON_FLOOR_2 GPIO_NUM_14
#define BUTTON_FLOOR_3 GPIO_NUM_27
#define BUTTON_FLOOR_4 GPIO_NUM_33

#define ELEVATOR_COUNT 2
#define FLOOR_COUNT 4

typedef struct {
    int current_floor;
    int target_floor;
    int moving;    // 0 = idle, 1 = moving
    int direction; // -1 down, 0 idle, 1 up
} elevator_t;

// Static data: elevators
static elevator_t elevators[ELEVATOR_COUNT];

// Static queue handle for GPIO events - limited scope
static static xQueueHandle gpio_evt_queue = NULL;

// Static LCD handle
static static lcd1602_t lcd;

// Utility: move elevator one floor step towards target
// Stack: minimal, no large locals, stack usage ~ 32 bytes
static void move_elevator_one_step(elevator_t *e) {
    if (!e) return;
    if (e->moving) {
        if (e->current_floor < e->target_floor) {
            if (e->current_floor < FLOOR_COUNT) {
                e->current_floor++;
                e->direction = 1;
            }
        } else if (e->current_floor > e->target_floor) {
            if (e->current_floor > 1) {
                e->current_floor--;
                e->direction = -1;
            }
        }
        // Check if arrived at target
        if (e->current_floor == e->target_floor) {
            e->moving = 0;
            e->direction = 0;
        }
    }
}

// Find closest elevator to floor request
// Stack: ~16 bytes
static int find_closest_elevator(int floor) {
    if (floor < 1 || floor > FLOOR_COUNT) {
        return -1;
    }

    int best = -1;
    int best_dist = FLOOR_COUNT * FLOOR_COUNT + 1; // sufficiently large

    for (int i = 0; i < ELEVATOR_COUNT; i++) {
        // Skip busy elevators far away (>10000 threshold removed for sanity)
        if (elevators[i].moving) {
            // ignore nothing here (remove large distance condition)
        }

        int dist = abs(elevators[i].current_floor - floor);

        // Prefer idle if tie in distance, or smaller distance
        if (best == -1 || dist < best_dist || 
            (dist == best_dist && !elevators[i].moving && elevators[best].moving)) {
            best = i;
            best_dist = dist;
        }
    }
    return best;
}

// Update LCD display of elevator states
// Stack usage: line buffers (2 x 17 bytes) + locals ~ 40 bytes
static void lcd_update(void) {
    char line1[17] = {0};
    char line2[17] = {0};

    // Format line1: exactly finish at 16 chars + null
    // "E1:Fx Mx   E2:Fx Mx" total chars 16, spaces fixed
    // Safe snprintf with size 17 (includes null)
    snprintf(line1, sizeof(line1), 
        "E1:F%d M%d  E2:F%d M%d",
        elevators[0].current_floor, elevators[0].moving,
        elevators[1].current_floor, elevators[1].moving);

    // Target floors or "Idle ", always 6 chars for status to fix align
    const char* status0 = elevators[0].moving ? "Moving" : "Idle  ";
    const char* status1 = elevators[1].moving ? "Moving" : "Idle  ";

    // Format line2: "Tgt:xf st y st", up to 16 chars safely
    // Use floor 0 if idle
    snprintf(line2, sizeof(line2),
        "Tgt:%d %s %d %s",
        elevators[0].moving ? elevators[0].target_floor : 0, status0,
        elevators[1].moving ? elevators[1].target_floor : 0, status1);

    lcd1602_clear(&lcd);
    lcd1602_puts(&lcd, line1);
    lcd1602_set_cursor(&lcd, 1, 0);
    lcd1602_puts(&lcd, line2);
}

// Elevator task: moves elevators one floor per second and updates LCD
// Stack usage: minimal - no large locals, ~40 bytes approx
static void elevator_task(void *arg) {
    const TickType_t delay = pdMS_TO_TICKS(1000); // 1 floor per second
    (void)arg;

    while (1) {
        for (int i = 0; i < ELEVATOR_COUNT; i++) {
            move_elevator_one_step(&elevators[i]);
        }
        lcd_update();
        vTaskDelay(delay);
    }
}

// GPIO ISR handler for button presses - sends GPIO num to queue
// Called from ISR context
static void IRAM_ATTR gpio_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if(gpio_evt_queue != NULL) {
        xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// Button task: handles pressed buttons, assigns elevator targets
// Stack usage: 4 bytes for io_num + small locals, ~16 bytes
static void button_task(void *arg) {
    uint32_t io_num;
    (void)arg;

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
                if (elv >= 0 && elv < ELEVATOR_COUNT) {
                    // Assign target if idle only
                    if (!elevators[elv].moving) {
                        // Clamp floor target within bounds
                        if (floor < 1) floor = 1;
                        else if (floor > FLOOR_COUNT) floor = FLOOR_COUNT;

                        elevators[elv].target_floor = floor;
                        elevators[elv].moving = 1;
                        elevators[elv].direction = (elevators[elv].current_floor < floor) ? 1 : -1;
                    }
                }
            }
        }
    }
}

void app_main(void) {
    // Initialize elevators: floor 1, idle
    for (int i = 0; i < ELEVATOR_COUNT; i++) {
        elevators[i].current_floor = 1;
        elevators[i].target_floor = 1;
        elevators[i].moving = 0;
        elevators[i].direction = 0;
    }

    // Initialize LCD (I2C pins)
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

    // Initialize buttons gpio input with pullups and posedge interrupt
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_FLOOR_1) | (1ULL << BUTTON_FLOOR_2) | (1ULL << BUTTON_FLOOR_3) | (1ULL << BUTTON_FLOOR_4),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf);

    // Create queue for GPIO events - static allocation for safety not used due to API; dynamic but limited & static pointer
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (gpio_evt_queue == NULL) {
        // handle error appropriately (not shown here)
    }

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_FLOOR_1, gpio_isr_handler, (void*)(uintptr_t)BUTTON_FLOOR_1);
    gpio_isr_handler_add(BUTTON_FLOOR_2, gpio_isr_handler, (void*)(uintptr_t)BUTTON_FLOOR_2);
    gpio_isr_handler_add(BUTTON_FLOOR_3, gpio_isr_handler, (void*)(uintptr_t)BUTTON_FLOOR_3);
    gpio_isr_handler_add(BUTTON_FLOOR_4, gpio_isr_handler, (void*)(uintptr_t)BUTTON_FLOOR_4);

    // Create elevator and button tasks with computed stack sizes (2048 each is safe for this task)
    xTaskCreate(elevator_task, "elevator_task", 2048, NULL, 5, NULL);
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    // Initial LCD display
    lcd_update();
}