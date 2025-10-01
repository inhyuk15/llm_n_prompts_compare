#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <limits.h>
#include <assert.h>

#define LED_PIN  10U  /* LED pin for product output */

#define LCD_COLS 16U
#define LCD_ROWS 2U

#define INPUT_BUFFER_SIZE 32U
#define MONEY_STR_SIZE 16U
#define PRODUCT_STR_SIZE 16U

/* Error messages */
#define ERR_INVALID_INPUT "Invalid input"
#define ERR_NOT_ENOUGH_MONEY "Not enough $"
#define MSG_WELCOME_LINE1 "Vending Machine"
#define MSG_WELCOME_LINE2 "Insert money+num"
#define MSG_ENTER "Enter money and product number:"
#define MSG_FORMAT "Format: <money> <product_num>"
#define MSG_EXAMPLE "Example: 2000 1"

typedef struct
{
    int32_t price;
    const char * name;
} Product;

static const Product products[] =
{
    {1500, "Cola"},
    {1200, "Water"},
    {1800, "Juice"},
    {1000, "Snack"}
};
static const uint8_t product_count = (uint8_t)(sizeof(products) / sizeof(products[0]));

static LiquidCrystal_I2C lcd(0x27, LCD_COLS, LCD_ROWS); /* I2C address 0x27, 16x2 LCD */

/* Volatile static buffers for input and parsing to avoid dynamic allocation */
static volatile char input_buffer[INPUT_BUFFER_SIZE];   /* Input line buffer (max 31 chars + null) */
static volatile char money_str[MONEY_STR_SIZE];         /* Money substring buffer */
static volatile char product_str[PRODUCT_STR_SIZE];     /* Product number substring buffer */

/* Forward declarations */
static int32_t parse_int(const char * str, uint8_t len);
static void lcd_printf_fixed(uint8_t col, uint8_t row, const char * fmt, const char * str);
static void lcd_print_int(uint8_t col, uint8_t row, const char * label, int32_t num);
static void print_error_and_wait(const char * msg);

static bool read_serial_input(void);
static void process_input_line(void);
static bool extract_tokens(const char * input, char * money_out, uint8_t * money_len_out,
                           char * product_out, uint8_t * product_len_out);
static void handle_valid_transaction(int32_t money, int32_t prod_num);
static void print_welcome_messages(void);

/* Helper function to enter a critical section and save interrupt state */
static inline uint8_t enter_critical_section(void)
{
    uint8_t sreg = SREG;
    cli();
    return sreg;
}

/* Helper function to exit a critical section restoring interrupt state */
static inline void exit_critical_section(uint8_t sreg)
{
    SREG = sreg;
}

/* Parsing function is already reentrant and safe */
static int32_t parse_int(const char * str, uint8_t len)
{
    assert(str != NULL);
    /* Convert digit string to integer safely
       Return -1 if invalid (non-digit or empty) */
    int32_t val;
    uint8_t i;

    if (str == NULL || len == 0U || len >= MONEY_STR_SIZE)
    {
        return -1;
    }

    val = 0;
    for (i = 0U; i < len; i++)
    {
        char c = str[i];
        if ((c < '0') || (c > '9'))
        {
            return -1;
        }
        /* Prevent overflow for 32-bit int */
        if (val > ((INT_MAX - (int32_t)(c - '0')) / 10))
        {
            return -1;
        }
        val = val * 10 + (int32_t)(c - '0');
    }
    return val;
}

static void lcd_printf_fixed(uint8_t col, uint8_t row, const char * fmt, const char * str)
{
    if (fmt == NULL)
    {
        return;
    }
    assert(col < LCD_COLS && row < LCD_ROWS);
    lcd.setCursor((int)col, (int)row);
    lcd.print(fmt);
    if (str != NULL)
    {
        lcd.print(str);
    }
}

static void lcd_print_int(uint8_t col, uint8_t row, const char * label, int32_t num)
{
    char buf[LCD_COLS + 1U];

    if (label == NULL)
    {
        return;
    }
    assert(col < LCD_COLS && row < LCD_ROWS);
    lcd.setCursor((int)col, (int)row);
    (void)snprintf(buf, (size_t)LCD_COLS + 1U, "%s%ld", label, (long)num);
    lcd.print(buf);
}

static void print_error_and_wait(const char * msg)
{
    if (msg == NULL)
    {
        return;
    }
    lcd.clear();
    lcd.print(msg);
    delay(1500);
    lcd.clear();
}

static bool read_serial_input(void)
{
    uint8_t i = 0U;

    if (Serial.available() == 0U)
    {
        return false;
    }

    /* Enter critical section to safely modify shared input_buffer */
    uint8_t _sreg = enter_critical_section();

    while ((Serial.available() != 0U) && (i < (INPUT_BUFFER_SIZE - 1U)))
    {
        int c_int = Serial.read();
        if (c_int == -1)
        {
            break;
        }
        char c_char = (char)c_int;
        if ((c_char == '\n') || (c_char == '\r'))
        {
            break;
        }
        ((char*)input_buffer)[i++] = c_char; /* cast to non-volatile for writing */
    }
    ((char*)input_buffer)[i] = '\0';

    exit_critical_section(_sreg);

    return true;
}

static bool extract_tokens(const char * input, char * money_out, uint8_t * money_len_out,
                           char * product_out, uint8_t * product_len_out)
{
    assert(input != NULL);
    assert(money_out != NULL);
    assert(product_out != NULL);

    const char * start = input;
    const char * space_ptr = NULL;
    uint8_t j;

    /* Trim leading spaces */
    while ((*start == ' ') && (*start != '\0'))
    {
        start++;
    }
    if (*start == '\0')
    {
        return false;
    }

    /* Find space separator */
    for (j = 0U; start[j] != '\0'; j++)
    {
        if (start[j] == ' ')
        {
            space_ptr = &start[j];
            break;
        }
    }
    if (space_ptr == NULL)
    {
        return false;
    }

    /* Extract money substring (trim trailing spaces) */
    uint8_t money_start_index = 0U;
    uint8_t money_end_index = (uint8_t)(space_ptr - start);

    while ((money_end_index > money_start_index) && (start[money_end_index - 1U] == ' '))
    {
        money_end_index--;
    }

    uint8_t money_len = (uint8_t)(money_end_index - money_start_index);

    /* Extract product substring (skip spaces after separator) */
    const char * prod_start = space_ptr + 1U;
    while ((*prod_start == ' ') && (*prod_start != '\0'))
    {
        prod_start++;
    }

    uint8_t prod_len = 0U;
    while ((prod_start[prod_len] != '\0') &&
           (prod_start[prod_len] != ' ') &&
           (prod_len < (PRODUCT_STR_SIZE - 1U)))
    {
        prod_len++;
    }

    /* Validate lengths */
    if ((money_len == 0U) || (money_len >= MONEY_STR_SIZE) || (prod_len == 0U) || (prod_len >= PRODUCT_STR_SIZE))
    {
        return false;
    }

    /* Copy money substring with boundary check */
    for (j = 0U; j < money_len && j < MONEY_STR_SIZE - 1U; j++)
    {
        money_out[j] = start[j];
    }
    money_out[(j < MONEY_STR_SIZE) ? j : (MONEY_STR_SIZE - 1U)] = '\0';

    /* Copy product substring with boundary check */
    for (j = 0U; j < prod_len && j < PRODUCT_STR_SIZE - 1U; j++)
    {
        product_out[j] = prod_start[j];
    }
    product_out[(j < PRODUCT_STR_SIZE) ? j : (PRODUCT_STR_SIZE - 1U)] = '\0';

    /* Output lengths */
    if (money_len_out != NULL)
    {
        *money_len_out = money_len;
    }
    if (product_len_out != NULL)
    {
        *product_len_out = prod_len;
    }

    return true;
}

static void handle_valid_transaction(int32_t money, int32_t prod_num)
{
    assert(prod_num >= 1);
    assert((uint8_t)prod_num <= product_count);
    const Product * selected;

    uint8_t product_idx = (uint8_t)(prod_num - 1);
    assert(product_idx < product_count);

    selected = &products[product_idx];

    /* Critical section for Serial and LCD output to prevent interleaving */
    uint8_t sreg = enter_critical_section();

    Serial.print("Selected: ");
    Serial.print(selected->name != NULL ? selected->name : "(null)");
    Serial.print(" Price: ");
    Serial.print(selected->price);
    Serial.print(" Money: ");
    Serial.println(money);

    lcd.clear();

    if (money < selected->price)
    {
        Serial.println("Not enough money.");
        lcd.print(ERR_NOT_ENOUGH_MONEY);

        exit_critical_section(sreg);

        delay(1500);

        sreg = enter_critical_section();
        lcd.clear();
        exit_critical_section(sreg);
    }
    else
    {
        /* Turn on LED to indicate product output */
        digitalWrite(LED_PIN, HIGH);
        lcd.setCursor(0, 0);
        {
            char line[LCD_COLS + 1U];
            /* Defensive snprintf usage */
            (void)snprintf(line, (size_t)(LCD_COLS + 1U), "Output: %.8s", selected->name != NULL ? selected->name : "");
            lcd.print(line);
        }

        exit_critical_section(sreg);

        delay(2000);

        digitalWrite(LED_PIN, LOW);

        /* Prevent overflow/underflow in change calculation */
        int32_t change = 0;
        if (money >= selected->price)
        {
            change = money - selected->price;
        }

        sreg = enter_critical_section();
        lcd.setCursor(0, 1);
        {
            char line[LCD_COLS + 1U];
            (void)snprintf(line, (size_t)(LCD_COLS + 1U), "Change: %ld Won", (long)change);
            lcd.print(line);
        }
        Serial.print("Change returned: ");
        Serial.println(change);

        exit_critical_section(sreg);

        delay(3000);

        sreg = enter_critical_section();
        lcd.clear();
        Serial.println(MSG_ENTER);
        exit_critical_section(sreg);
    }
}

static void process_input_line(void)
{
    bool input_valid = true;
    int32_t money = -1;
    int32_t prod_num = -1;
    uint8_t money_len = 0U, product_len = 0U;
    char local_input_buffer[INPUT_BUFFER_SIZE];
    char local_money_str[MONEY_STR_SIZE];
    char local_product_str[PRODUCT_STR_SIZE];

    /* Copy volatile input buffer atomically */
    uint8_t sreg = enter_critical_section();
    for(uint8_t i = 0; i < INPUT_BUFFER_SIZE; i++) {
        local_input_buffer[i] = input_buffer[i];
    }
    exit_critical_section(sreg);

    if (!extract_tokens(local_input_buffer, local_money_str, &money_len, local_product_str, &product_len))
    {
        input_valid = false;
    }
    else
    {
        money = parse_int(local_money_str, money_len);
        prod_num = parse_int(local_product_str, product_len);

        if ((money < 0) || (prod_num < 1) || ((uint8_t)prod_num > product_count))
        {
            input_valid = false;
        }
    }

    if (!input_valid)
    {
        sreg = enter_critical_section();
        Serial.println("Invalid input format.");
        exit_critical_section(sreg);

        print_error_and_wait(ERR_INVALID_INPUT);
    }
    else
    {
        handle_valid_transaction(money, prod_num);
    }
}

static void print_welcome_messages(void)
{
    lcd.setCursor(0, 0);
    lcd.print(MSG_WELCOME_LINE1);
    lcd.setCursor(0, 1);
    lcd.print(MSG_WELCOME_LINE2);
    delay(2000);
    lcd.clear();

    Serial.println(MSG_ENTER);
    Serial.println(MSG_FORMAT);
    Serial.println(MSG_EXAMPLE);
}

void setup(void)
{
    Serial.begin(115200);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    lcd.init();
    lcd.backlight();
    lcd.clear();

    print_welcome_messages();
}

void loop(void)
{
    if (read_serial_input())
    {
        process_input_line();
    }
}