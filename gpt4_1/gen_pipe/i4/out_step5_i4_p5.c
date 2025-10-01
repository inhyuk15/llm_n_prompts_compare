#include <stdio.h>
#include <stdlib.h> // for abs()
#include <assert.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
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
    volatile int32_t current_floor;
    volatile int32_t target_floor;
    volatile int32_t moving;    /* 0 = idle, 1 = moving */
    volatile int32_t direction; /* -1 down, 0 idle, 1 up */
} elevator_t;

/* Static data: elevators */
static elevator_t elevators[ELEVATOR_COUNT];

/* Protect elevators array for multithreaded access */
static SemaphoreHandle_t elevators_mutex = NULL;

/* Static queue handle for GPIO events - limited scope */
static QueueHandle_t gpio_evt_queue = NULL;

/* Static LCD handle */
static lcd1602_t lcd;

/* Elevator movement helpers */
static void move_elevator_up(elevator_t * const e)
{
    assert(e != NULL);
    if (e->current_floor < FLOOR_COUNT) {
        assert(e->current_floor + 1 <= FLOOR_COUNT);
        e->current_floor++;
        e->direction = 1;
    }
}

static void move_elevator_down(elevator_t * const e)
{
    assert(e != NULL);
    if (e->current_floor > 1) {
        assert(e->current_floor - 1 >= 1);
        e->current_floor--;
        e->direction = -1;
    }
}

/* Utility: move elevator one floor step towards target */
static void move_elevator_one_step(elevator_t * const e)
{
    if (e == NULL) {
        return;
    }
    assert(e->current_floor >= 1 && e->current_floor <= FLOOR_COUNT);
    assert(e->target_floor >= 1 && e->target_floor <= FLOOR_COUNT);
    assert(e->moving == 0 || e->moving == 1);
    assert(e->direction >= -1 && e->direction <= 1);

    if (e->moving != 0) {
        if (e->current_floor < e->target_floor) {
            move_elevator_up(e);
        } else if (e->current_floor > e->target_floor) {
            move_elevator_down(e);
        }

        if (e->current_floor == e->target_floor) {
            e->moving = 0;
            e->direction = 0;
        }
    }

    assert(e->current_floor >= 1 && e->current_floor <= FLOOR_COUNT);
    assert(e->direction >= -1 && e->direction <= 1);
}

/* Helper to clamp floor within bounds */
static int32_t clamp_floor(int32_t floor)
{
    if (floor < 1) {
        return 1;
    }
    if (floor > FLOOR_COUNT) {
        return FLOOR_COUNT;
    }
    return floor;
}

/* Find closest elevator to floor request */
static int32_t find_closest_elevator(int32_t floor)
{
    if ((floor < 1) || (floor > FLOOR_COUNT)) {
        return -1;
    }

    int32_t best = -1;
    int32_t best_dist = (FLOOR_COUNT * FLOOR_COUNT) + 1;

    for (int32_t i = 0; i < (int32_t)ELEVATOR_COUNT; i++) {
        /* Acquire elevator mutex to safely read elevator states */
        if (xSemaphoreTake(elevators_mutex, portMAX_DELAY) == pdTRUE) {
            int32_t curr_floor = elevators[i].current_floor;
            int32_t moving = elevators[i].moving;
            xSemaphoreGive(elevators_mutex);

            assert(curr_floor >= 1 && curr_floor <= FLOOR_COUNT);

            const int32_t dist = abs(curr_floor - floor);

            if ((best == -1) ||
                (dist < best_dist) ||
                ((dist == best_dist) &&
                 (moving == 0))) {
                best = i;
                best_dist = dist;
            }
        }
    }

    assert(best >= -1 && best < (int32_t)ELEVATOR_COUNT);
    return best;
}

/* Format line 1 for LCD */
static void lcd_format_line1(char *buf, size_t size)
{
    assert(buf != NULL);
    assert(size > 0);

    if (xSemaphoreTake(elevators_mutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < (int)ELEVATOR_COUNT; i++) {
            assert(elevators[i].current_floor >= 1 && elevators[i].current_floor <= FLOOR_COUNT);
            assert(elevators[i].moving == 0 || elevators[i].moving == 1);
        }

        (void)snprintf(buf, size,
                       "E1:F%d M%d  E2:F%d M%d",
                       elevators[0].current_floor,
                       elevators[0].moving,
                       elevators[1].current_floor,
                       elevators[1].moving);

        xSemaphoreGive(elevators_mutex);
    } else {
        buf[0] = '\0';
    }
}

/* Format line 2 for LCD */
static void lcd_format_line2(char *buf, size_t size)
{
    assert(buf != NULL);
    assert(size > 0);

    if (xSemaphoreTake(elevators_mutex, portMAX_DELAY) == pdTRUE) {
        const char *status0 = (elevators[0].moving != 0) ? "Moving" : "Idle  ";
        const char *status1 = (elevators[1].moving != 0) ? "Moving" : "Idle  ";
        int32_t tgt0 = (elevators[0].moving != 0) ? elevators[0].target_floor : 0;
        int32_t tgt1 = (elevators[1].moving != 0) ? elevators[1].target_floor : 0;

        if (tgt0 != 0) {
            assert(tgt0 >= 1 && tgt0 <= FLOOR_COUNT);
        }
        if (tgt1 != 0) {
            assert(tgt1 >= 1 && tgt1 <= FLOOR_COUNT);
        }

        (void)snprintf(buf, size,
                       "Tgt:%d %s %d %s",
                       tgt0,
                       status0,
                       tgt1,
                       status1);

        xSemaphoreGive(elevators_mutex);
    } else {
        buf[0] = '\0';
    }
}

/* Update LCD display of elevator states */
static void lcd_update(void)
{
    char line1[LCD_LINE_LENGTH] = {0};
    char line2[LCD_LINE_LENGTH] = {0};

    lcd_format_line1(line1, sizeof(line1));
    lcd_format_line2(line2, sizeof(line2));

    lcd1602_clear(&lcd);
    lcd1602_puts(&lcd, line1);
    lcd1602_set_cursor(&lcd, 1U, 0U);
    lcd1602_puts(&lcd, line2);
}

/* Process a floor number request */
static void process_floor_request(int32_t floor)
{
    if (floor <= 0) {
        return;
    }
    if (floor > FLOOR_COUNT) {
        return;
    }

    if (xSemaphoreTake(elevators_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    int32_t elv = find_closest_elevator(floor);

    /* Validate elevator index */
    if (elv < 0 || elv >= (int32_t)ELEVATOR_COUNT) {
        xSemaphoreGive(elevators_mutex);
        return;
    }

    /* Assign target only if idle */
    if (elevators[elv].moving == 0) {
        floor = clamp_floor(floor);
        elevators[elv].target_floor = floor;
        elevators[elv].moving = 1;

        if (elevators[elv].current_floor < floor) {
            elevators[elv].direction = 1;
        } else if (elevators[elv].current_floor > floor) {
            elevators[elv].direction = -1;
        } else {
            elevators[elv].direction = 0;
        }

        assert(elevators[elv].target_floor >= 1 && elevators[elv].target_floor <= FLOOR_COUNT);
        assert(elevators[elv].direction >= -1 && elevators[elv].direction <= 1);
    }
    xSemaphoreGive(elevators_mutex);
}

/* Convert GPIO number to floor */
static int32_t gpio_to_floor(uint32_t io_num)
{
    switch (io_num) {
        case BUTTON_FLOOR_1: return 1;
        case BUTTON_FLOOR_2: return 2;
        case BUTTON_FLOOR_3: return 3;
        case BUTTON_FLOOR_4: return 4;
        default: return 0;
    }
}

/* Elevator task: moves elevators one floor per second and updates LCD */
static void elevator_task(void * arg)
{
    (void)arg;
    const TickType_t delay = pdMS_TO_TICKS(1000U);
    BaseType_t task_continue = pdTRUE;

    while (task_continue != pdFALSE) {
        if (xSemaphoreTake(elevators_mutex, portMAX_DELAY) == pdTRUE) {
            for (int32_t i = 0; i < (int32_t)ELEVATOR_COUNT; i++) {
                move_elevator_one_step(&elevators[i]);
            }
            xSemaphoreGive(elevators_mutex);
        }
        lcd_update();
        vTaskDelay(delay);
    }
}

/* GPIO ISR handler for button presses - sends GPIO num to queue */
static void IRAM_ATTR gpio_isr_handler(void * arg)
{
    if (gpio_evt_queue == NULL) {
        return;
    }
    const uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    (void)xQueueSendFromISR(gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken != pdFALSE) {
        portYIELD_FROM_ISR();
    }
}

/* Button processing: handle floor button pressed */
static void handle_button_press(uint32_t io_num)
{
    int32_t floor = gpio_to_floor(io_num);
    if (floor <= 0 || floor > FLOOR_COUNT) {
        return;
    }
    process_floor_request(floor);
}

/* Button task: handles pressed buttons, assigns elevator targets */
static void button_task(void * arg)
{
    (void)arg;
    uint32_t io_num;
    BaseType_t task_continue = pdTRUE;

    while (task_continue != pdFALSE) {
        if (gpio_evt_queue == NULL) {
            break;
        }

        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY) != pdFALSE) {
            handle_button_press(io_num);
        }
    }
}

/* Initialize elevators */
static void elevators_init(void)
{
    for (int32_t idx = 0; idx < (int32_t)ELEVATOR_COUNT; idx++) {
        elevators[idx].current_floor = 1;
        elevators[idx].target_floor  = 1;
        elevators[idx].moving        = 0;
        elevators[idx].direction     = 0;
    }
}

/* Initialize I2C for LCD */
static void i2c_init_for_lcd(void)
{
    i2c_config_t i2c_conf = {0};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = GPIO_NUM_21;
    i2c_conf.scl_io_num = GPIO_NUM_22;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = I2C_MASTER_FREQ_HZ;

    (void)i2c_param_config(I2C_NUM_0, &i2c_conf);
    (void)i2c_driver_install(I2C_NUM_0, i2c_conf.mode, 0U, 0U, 0U);
}

/* Initialize GPIO buttons */
static void gpio_buttons_init(void)
{
    gpio_config_t io_conf = {0};

    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = ((1ULL << BUTTON_FLOOR_1) |
                           (1ULL << BUTTON_FLOOR_2) |
                           (1ULL << BUTTON_FLOOR_3) |
                           (1ULL << BUTTON_FLOOR_4));
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    (void)gpio_config(&io_conf);
}

/* Setup GPIO ISR handlers for buttons */
static void gpio_isr_setup(void)
{
    (void)gpio_install_isr_service(GPIO_ISR_FLAGS);

    (void)gpio_isr_handler_add(BUTTON_FLOOR_1, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_1);
    (void)gpio_isr_handler_add(BUTTON_FLOOR_2, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_2);
    (void)gpio_isr_handler_add(BUTTON_FLOOR_3, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_3);
    (void)gpio_isr_handler_add(BUTTON_FLOOR_4, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_4);
}

/* Create tasks for elevator and button handling */
static void create_tasks(void)
{
    (void)xTaskCreate(elevator_task, "elevator_task", 2048U, NULL, 5, NULL);
    (void)xTaskCreate(button_task, "button_task", 2048U, NULL, 10, NULL);
}

/* Main application entry */
void app_main(void)
{
    /* Create mutex for elevator data protection */
    elevators_mutex = xSemaphoreCreateMutex();
    assert(elevators_mutex != NULL);

    /* Initialize elevators */
    elevators_init();

    /* Initialize I2C and LCD */
    i2c_init_for_lcd();
    lcd1602_init(&lcd, I2C_NUM_0, 0x27U);

    /* Initialize GPIO buttons */
    gpio_buttons_init();

    /* Create queue for GPIO events */
    gpio_evt_queue = xQueueCreate(GPIO_EVT_QUEUE_LEN, sizeof(uint32_t));
    if (gpio_evt_queue != NULL) {
        gpio_isr_setup();
        create_tasks();
        lcd_update();
    } else {
        assert(0 && "GPIO event queue creation failed");
    }
}