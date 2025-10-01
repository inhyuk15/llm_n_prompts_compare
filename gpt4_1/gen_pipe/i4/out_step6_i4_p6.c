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

/* Error codes for elevator system */
typedef enum {
    ELEV_OK = 0,              /* Success */
    ELEV_ERR_INVALID_ARG,     /* Invalid argument */
    ELEV_ERR_MUTEX_FAIL,      /* Mutex acquisition failure */
    ELEV_ERR_QUEUE_FAIL,      /* Queue operation failure */
    ELEV_ERR_NOT_FOUND,       /* Elevator not found */
    ELEV_ERR_FATAL            /* Fatal error */
} elev_err_t;

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
static elev_err_t move_elevator_up(elevator_t * const e)
{
    if (e == NULL) {
        return ELEV_ERR_INVALID_ARG;
    }
    if (e->current_floor < FLOOR_COUNT) {
        if ((e->current_floor + 1) <= FLOOR_COUNT) {
            e->current_floor++;
            e->direction = 1;
            return ELEV_OK;
        } else {
            return ELEV_ERR_FATAL;
        }
    }
    return ELEV_ERR_INVALID_ARG; /* Cannot move up */
}

static elev_err_t move_elevator_down(elevator_t * const e)
{
    if (e == NULL) {
        return ELEV_ERR_INVALID_ARG;
    }
    if (e->current_floor > 1) {
        if ((e->current_floor - 1) >= 1) {
            e->current_floor--;
            e->direction = -1;
            return ELEV_OK;
        } else {
            return ELEV_ERR_FATAL;
        }
    }
    return ELEV_ERR_INVALID_ARG; /* Cannot move down */
}

/* Utility: move elevator one floor step towards target */
static elev_err_t move_elevator_one_step(elevator_t * const e)
{
    if (e == NULL) {
        return ELEV_ERR_INVALID_ARG;
    }

    /* Sanity checks */
    if (e->current_floor < 1 || e->current_floor > FLOOR_COUNT ||
        e->target_floor < 1 || e->target_floor > FLOOR_COUNT ||
        (e->moving != 0 && e->moving != 1) ||
        (e->direction < -1 || e->direction > 1)) {
        return ELEV_ERR_FATAL;
    }

    if (e->moving != 0) {
        elev_err_t err = ELEV_OK;
        if (e->current_floor < e->target_floor) {
            err = move_elevator_up(e);
            if (err != ELEV_OK) return err;
        } else if (e->current_floor > e->target_floor) {
            err = move_elevator_down(e);
            if (err != ELEV_OK) return err;
        }

        if (e->current_floor == e->target_floor) {
            e->moving = 0;
            e->direction = 0;
        }
    }

    /* Final sanity */
    if (e->current_floor < 1 || e->current_floor > FLOOR_COUNT ||
        e->direction < -1 || e->direction > 1) {
        return ELEV_ERR_FATAL;
    }

    return ELEV_OK;
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
static elev_err_t find_closest_elevator(int32_t floor, int32_t *closest_idx)
{
    if (closest_idx == NULL) {
        return ELEV_ERR_INVALID_ARG;
    }

    if ((floor < 1) || (floor > FLOOR_COUNT)) {
        *closest_idx = -1;
        return ELEV_ERR_INVALID_ARG;
    }

    int32_t best = -1;
    int32_t best_dist = (FLOOR_COUNT * FLOOR_COUNT) + 1;

    /* Acquire elevator mutex to safely read elevator states */
    if (xSemaphoreTake(elevators_mutex, portMAX_DELAY) != pdTRUE) {
        *closest_idx = -1;
        return ELEV_ERR_MUTEX_FAIL;
    }

    for (int32_t i = 0; i < (int32_t)ELEVATOR_COUNT; i++) {
        int32_t curr_floor = elevators[i].current_floor;
        int32_t moving = elevators[i].moving;

        if (curr_floor < 1 || curr_floor > FLOOR_COUNT) {
            /* Continue with next, corrupted state */
            continue;
        }

        const int32_t dist = abs(curr_floor - floor);

        if ((best == -1) ||
            (dist < best_dist) ||
            ((dist == best_dist) && (moving == 0))) {
            best = i;
            best_dist = dist;
        }
    }
    xSemaphoreGive(elevators_mutex);

    if (best == -1) {
        *closest_idx = -1;
        return ELEV_ERR_NOT_FOUND;
    }

    *closest_idx = best;
    return ELEV_OK;
}

/* Format line 1 for LCD */
static elev_err_t lcd_format_line1(char *buf, size_t size)
{
    if (buf == NULL || size == 0) {
        return ELEV_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(elevators_mutex, portMAX_DELAY) != pdTRUE) {
        if (size > 0) buf[0] = '\0';
        return ELEV_ERR_MUTEX_FAIL;
    }

    for (int i = 0; i < (int)ELEVATOR_COUNT; i++) {
        if (elevators[i].current_floor < 1 || elevators[i].current_floor > FLOOR_COUNT) {
            /* corrupted state, release mutex and fail */
            xSemaphoreGive(elevators_mutex);
            buf[0] = '\0';
            return ELEV_ERR_FATAL;
        }
        if (elevators[i].moving != 0 && elevators[i].moving != 1) {
            xSemaphoreGive(elevators_mutex);
            buf[0] = '\0';
            return ELEV_ERR_FATAL;
        }
    }

    (void)snprintf(buf, size,
                   "E1:F%d M%d  E2:F%d M%d",
                   elevators[0].current_floor,
                   elevators[0].moving,
                   elevators[1].current_floor,
                   elevators[1].moving);

    xSemaphoreGive(elevators_mutex);
    return ELEV_OK;
}

/* Format line 2 for LCD */
static elev_err_t lcd_format_line2(char *buf, size_t size)
{
    if (buf == NULL || size == 0) {
        return ELEV_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(elevators_mutex, portMAX_DELAY) != pdTRUE) {
        if (size > 0) buf[0] = '\0';
        return ELEV_ERR_MUTEX_FAIL;
    }

    const char *status0 = (elevators[0].moving != 0) ? "Moving" : "Idle  ";
    const char *status1 = (elevators[1].moving != 0) ? "Moving" : "Idle  ";
    int32_t tgt0 = (elevators[0].moving != 0) ? elevators[0].target_floor : 0;
    int32_t tgt1 = (elevators[1].moving != 0) ? elevators[1].target_floor : 0;

    if (tgt0 != 0 && (tgt0 < 1 || tgt0 > FLOOR_COUNT)) {
        xSemaphoreGive(elevators_mutex);
        buf[0] = '\0';
        return ELEV_ERR_FATAL;
    }
    if (tgt1 != 0 && (tgt1 < 1 || tgt1 > FLOOR_COUNT)) {
        xSemaphoreGive(elevators_mutex);
        buf[0] = '\0';
        return ELEV_ERR_FATAL;
    }

    (void)snprintf(buf, size,
                   "Tgt:%d %s %d %s",
                   tgt0,
                   status0,
                   tgt1,
                   status1);

    xSemaphoreGive(elevators_mutex);
    return ELEV_OK;
}

/* Update LCD display of elevator states */
static elev_err_t lcd_update(void)
{
    char line1[LCD_LINE_LENGTH] = {0};
    char line2[LCD_LINE_LENGTH] = {0};
    elev_err_t err;

    err = lcd_format_line1(line1, sizeof(line1));
    if (err != ELEV_OK) {
        line1[0] = '\0'; /* fail safe */
    }

    err = lcd_format_line2(line2, sizeof(line2));
    if (err != ELEV_OK) {
        line2[0] = '\0'; /* fail safe */
    }

    lcd1602_clear(&lcd);
    lcd1602_puts(&lcd, line1);
    lcd1602_set_cursor(&lcd, 1U, 0U);
    lcd1602_puts(&lcd, line2);

    return ELEV_OK;
}

/* Process a floor number request */
static elev_err_t process_floor_request(int32_t floor)
{
    if (floor < 1 || floor > FLOOR_COUNT) {
        return ELEV_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(elevators_mutex, portMAX_DELAY) != pdTRUE) {
        return ELEV_ERR_MUTEX_FAIL;
    }

    int32_t elv = -1;
    elev_err_t err = find_closest_elevator(floor, &elv);

    if (err != ELEV_OK || elv < 0 || elv >= (int32_t)ELEVATOR_COUNT) {
        xSemaphoreGive(elevators_mutex);
        return ELEV_ERR_NOT_FOUND;
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

        if (elevators[elv].target_floor < 1 || elevators[elv].target_floor > FLOOR_COUNT ||
            elevators[elv].direction < -1 || elevators[elv].direction > 1) {
            xSemaphoreGive(elevators_mutex);
            return ELEV_ERR_FATAL;
        }
    }

    xSemaphoreGive(elevators_mutex);
    return ELEV_OK;
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
                (void)move_elevator_one_step(&elevators[i]);
                /* If needed, add further error handling here */
            }
            xSemaphoreGive(elevators_mutex);
        }
        (void)lcd_update();
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
    if (floor < 1 || floor > FLOOR_COUNT) {
        return;
    }
    (void)process_floor_request(floor);
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
static elev_err_t i2c_init_for_lcd(void)
{
    i2c_config_t i2c_conf = {0};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = GPIO_NUM_21;
    i2c_conf.scl_io_num = GPIO_NUM_22;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = I2C_MASTER_FREQ_HZ;

    esp_err_t err;
    err = i2c_param_config(I2C_NUM_0, &i2c_conf);
    if (err != ESP_OK) {
        return ELEV_ERR_FATAL;
    }
    err = i2c_driver_install(I2C_NUM_0, i2c_conf.mode, 0U, 0U, 0U);
    if (err != ESP_OK) {
        return ELEV_ERR_FATAL;
    }
    return ELEV_OK;
}

/* Initialize GPIO buttons */
static elev_err_t gpio_buttons_init(void)
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
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return ELEV_ERR_FATAL;
    }
    return ELEV_OK;
}

/* Setup GPIO ISR handlers for buttons */
static elev_err_t gpio_isr_setup(void)
{
    esp_err_t err;
    err = gpio_install_isr_service(GPIO_ISR_FLAGS);
    if (err != ESP_OK) {
        return ELEV_ERR_FATAL;
    }

    err = gpio_isr_handler_add(BUTTON_FLOOR_1, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_1);
    if (err != ESP_OK) {
        return ELEV_ERR_FATAL;
    }
    err = gpio_isr_handler_add(BUTTON_FLOOR_2, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_2);
    if (err != ESP_OK) {
        return ELEV_ERR_FATAL;
    }
    err = gpio_isr_handler_add(BUTTON_FLOOR_3, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_3);
    if (err != ESP_OK) {
        return ELEV_ERR_FATAL;
    }
    err = gpio_isr_handler_add(BUTTON_FLOOR_4, gpio_isr_handler, (void *)(uintptr_t)BUTTON_FLOOR_4);
    if (err != ESP_OK) {
        return ELEV_ERR_FATAL;
    }
    return ELEV_OK;
}

/* Create tasks for elevator and button handling */
static elev_err_t create_tasks(void)
{
    BaseType_t res;
    res = xTaskCreate(elevator_task, "elevator_task", 2048U, NULL, 5, NULL);
    if (res != pdPASS) {
        return ELEV_ERR_FATAL;
    }
    res = xTaskCreate(button_task, "button_task", 2048U, NULL, 10, NULL);
    if (res != pdPASS) {
        return ELEV_ERR_FATAL;
    }
    return ELEV_OK;
}

/* Main application entry */
void app_main(void)
{
    elev_err_t err;

    /* Create mutex for elevator data protection */
    elevators_mutex = xSemaphoreCreateMutex();
    if (elevators_mutex == NULL) {
        /* Fatal */
        assert(0 && "Failed to create elevators_mutex");
        return;
    }

    /* Initialize elevators */
    elevators_init();

    /* Initialize I2C and LCD */
    err = i2c_init_for_lcd();
    if (err != ELEV_OK) {
        assert(0 && "I2C init failed");
        return;
    }
    lcd1602_init(&lcd, I2C_NUM_0, 0x27U);

    /* Initialize GPIO buttons */
    err = gpio_buttons_init();
    if (err != ELEV_OK) {
        assert(0 && "GPIO buttons init failed");
        return;
    }

    /* Create queue for GPIO events */
    gpio_evt_queue = xQueueCreate(GPIO_EVT_QUEUE_LEN, sizeof(uint32_t));
    if (gpio_evt_queue == NULL) {
        assert(0 && "GPIO event queue creation failed");
        return;
    }

    err = gpio_isr_setup();
    if (err != ELEV_OK) {
        assert(0 && "GPIO ISR setup failed");
        return;
    }

    err = create_tasks();
    if (err != ELEV_OK) {
        assert(0 && "Task creation failed");
        return;
    }

    (void)lcd_update();
}