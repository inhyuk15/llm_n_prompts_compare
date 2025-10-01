#include <stdio.h>
#include <stdlib.h> // for abs()
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lcd1602_i2c.h"

/* Named constants */
#define BUTTON_FLOOR_1     GPIO_NUM_12
#define BUTTON_FLOOR_2     GPIO_NUM_14
#define BUTTON_FLOOR_3     GPIO_NUM_27
#define BUTTON_FLOOR_4     GPIO_NUM_33

#define ELEVATOR_COUNT     2
#define FLOOR_COUNT        4

#define LCD_LINE_LENGTH    17U
#define GPIO_EVT_QUEUE_LEN 10U
#define GPIO_ISR_FLAGS     0U
#define I2C_MASTER_FREQ_HZ 100000U

typedef struct {
    int32_t current_floor;
    int32_t target_floor;
    int32_t moving;    /* 0 = idle, 1 = moving */
    int32_t direction; /* -1 down, 0 idle, 1 up */
} elevator_t;

/* Static data: elevators */
static elevator_t elevators[ELEVATOR_COUNT];

/* Static queue handle for GPIO events - limited scope */
static QueueHandle_t gpio_evt_queue = NULL;

/* Static LCD handle */
static lcd1602_t lcd;

/* Utility: move elevator one floor step towards target */
/* Stack: minimal, no large locals, stack usage ~32 bytes */
static void move_elevator_one_step(elevator_t * const e)
{
    if (e == NULL) {
        return;
    }

    if (e->moving != 0) {
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

        /* Check if arrived at target */
        if (e->current_floor == e->target_floor) {
            e->moving = 0;
            e->direction = 0;
        }
    }
}

/* Find closest elevator to floor request */
/* Stack: ~16 bytes */
static int32_t find_closest_elevator(int32_t floor)
{
    int32_t best = -1;
    int32_t best_dist = (FLOOR_COUNT * FLOOR_COUNT) + 1;

    if ((floor < 1) || (floor > FLOOR_COUNT)) {
        best = -1;
    } else {
        int32_t i;
        for (i = 0; i < (int32_t)ELEVATOR_COUNT; i++) {
            const int32_t dist = abs(elevators[i].current_floor - floor);

            if ((best == -1) ||
                (dist < best_dist) ||
                ((dist == best_dist) &&
                 (elevators[i].moving == 0) &&
                 (elevators[best].moving != 0))) {
                best = i;
                best_dist = dist;
            }
        }
    }

    return best;
}

/* Update LCD display of elevator states */
/* Stack usage: line buffers (2 x 17 bytes) + locals ~ 40 bytes */
static void lcd_update(void)
{
    char line1[LCD_LINE_LENGTH] = {0};
    char line2[LCD_LINE_LENGTH] = {0};
    const char * status0 = NULL;
    const char * status1 = NULL;
    int32_t tgt0 = 0;
    int32_t tgt1 = 0;

    /* Format line1: exactly finish at 16 chars + null */
    /* "E1:Fx Mx   E2:Fx Mx" total chars 16, spaces fixed */
    /* Safe snprintf with size 17 (includes null) */
    (void)snprintf(line1, (sizeof(line1) / sizeof(line1[0])),
                   "E1:F%d M%d  E2:F%d M%d",
                   elevators[0].current_floor,
                   elevators[0].moving,
                   elevators[1].current_floor,
                   elevators[1].moving);

    if (elevators[0].moving != 0) {
        status0 = "Moving";
        tgt0 = elevators[0].target_floor;
    } else {
        status0 = "Idle  ";
        tgt0 = 0;
    }

    if (elevators[1].moving != 0) {
        status1 = "Moving";
        tgt1 = elevators[1].target_floor;
    } else {
        status1 = "Idle  ";
        tgt1 = 0;
    }

    /* Format line2: "Tgt:xf st y st", up to 16 chars safely */
    (void)snprintf(line2, (sizeof(line2) / sizeof(line2[0])),
                   "Tgt:%d %s %d %s",
                   tgt0,
                   status0, 
                   tgt1, 
                   status1);

    lcd1602_clear(&lcd);
    lcd1602_puts(&lcd, line1);
    lcd1602_set_cursor(&lcd, 1U, 0U);
    lcd1602_puts(&lcd, line2);
}

/* Elevator task: moves elevators one floor per second and updates LCD */
/* Stack usage: minimal - no large locals, ~40 bytes approx */
static void elevator_task(void * arg)
{
    const TickType_t delay = pdMS_TO_TICKS(1000U);
    (void)arg;
    BaseType_t task_continue = pdTRUE;

    while (task_continue != pdFALSE) {
        int32_t i;
        for (i = 0; i < (int32_t)ELEVATOR_COUNT; i++) {
            move_elevator_one_step(&elevators[i]);
        }
        lcd_update();
        vTaskDelay(delay);
    }
}

/* GPIO ISR handler for button presses - sends GPIO num to queue */
/* Called from ISR context */
static void IRAM_ATTR gpio_isr_handler(void * arg)
{
    const uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (gpio_evt_queue != NULL) {
        (void)xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    }

    if (xHigherPriorityTaskWoken != pdFALSE) {
        portYIELD_FROM_ISR();
    }
}

/* Button task: handles pressed buttons, assigns elevator targets */
/* Stack usage: 4 bytes for io_num + small locals, ~16 bytes */
static void button_task(void * arg)
{
    uint32_t io_num;
    (void)arg;
    BaseType_t task_continue = pdTRUE;

    while (task_continue != pdFALSE) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY) != pdFALSE) {
            int32_t floor = 0;
            switch (io_num) {
                case BUTTON_FLOOR_1:
                    floor = 1;
                    break;
                case BUTTON_FLOOR_2:
                    floor = 2;
                    break;
                case BUTTON_FLOOR_3:
                    floor = 3;
                    break;
                case BUTTON_FLOOR_4:
                    floor = 4;
                    break;
                default:
                    floor = 0;
                    break;
            }

            if (floor > 0) {
                int32_t elv = find_closest_elevator(floor);
                if ((elv >= 0) && (elv < (int32_t)ELEVATOR_COUNT)) {
                    /* Assign target if idle only */
                    if (elevators[elv].moving == 0) {
                        /* Clamp floor target within bounds */
                        if (floor < 1) {
                            floor = 1;
                        } else if (floor > FLOOR_COUNT) {
                            floor = FLOOR_COUNT;
                        }

                        elevators[elv].target_floor = floor;
                        elevators[elv].moving = 1;
                        if (elevators[elv].current_floor < floor) {
                            elevators[elv].direction = 1;
                        } else if (elevators[elv].current_floor > floor) {
                            elevators[elv].direction = -1;
                        } else {
                            elevators[elv].direction = 0;
                        }
                    }
                }
            }
        }
    }
}

/* Application main entry */
void app_main(void)
{
    int32_t idx;
    BaseType_t gpio_queue_created;
    i2c_config_t i2c_conf = {0};
    gpio_config_t io_conf = {0};

    /* Initialize elevators: floor 1, idle */
    for (idx = 0; idx < (int32_t)ELEVATOR_COUNT; idx++) {
        elevators[idx].current_floor = 1;
        elevators[idx].target_floor  = 1;
        elevators[idx].moving        = 0;
        elevators[idx].direction     = 0;
    }

    /* Initialize I2C */
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = GPIO_NUM_21;
    i2c_conf.scl_io_num = GPIO_NUM_22;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = I2C_MASTER_FREQ_HZ;

    (void)i2c_param_config(I2C_NUM_0, &i2c_conf);
    (void)i2c_driver_install(I2C_NUM_0, i2c_conf.mode, 0U, 0U, 0U);

    lcd1602_init(&lcd, I2C_NUM_0, 0x27U);

    /* Configure buttons GPIO input with pullups and posedge interrupt */
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = ((1ULL << BUTTON_FLOOR_1) |
                           (1ULL << BUTTON_FLOOR_2) |
                           (1ULL << BUTTON_FLOOR_3) |
                           (1ULL << BUTTON_FLOOR_4));
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    (void)gpio_config(&io_conf);

    /* Create queue for GPIO events */
    gpio_evt_queue = xQueueCreate(GPIO_EVT_QUEUE_LEN, sizeof(uint32_t));
    gpio_queue_created = (gpio_evt_queue != NULL) ? pdTRUE : pdFALSE;

    /* Only proceed if queue creation succeeded */
    if (gpio_queue_created != pdFALSE) {
        (void)gpio_install_isr_service(GPIO_ISR_FLAGS);

        (void)gpio_isr_handler_add(BUTTON_FLOOR_1, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_1);
        (void)gpio_isr_handler_add(BUTTON_FLOOR_2, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_2);
        (void)gpio_isr_handler_add(BUTTON_FLOOR_3, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_3);
        (void)gpio_isr_handler_add(BUTTON_FLOOR_4, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_4);

        (void)xTaskCreate(elevator_task, "elevator_task", 2048U, NULL, 5, NULL);
        (void)xTaskCreate(button_task, "button_task", 2048U, NULL, 10, NULL);

        /* Initial LCD display */
        lcd_update();
    } else {
        /* Handle queue creation failure if needed */
        /* Implementation left empty as per instruction */
    }
}