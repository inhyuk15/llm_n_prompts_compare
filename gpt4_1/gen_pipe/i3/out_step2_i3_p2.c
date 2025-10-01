#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <limits.h>

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

/*
  stack usage estimates:
  - loop function uses fixed buffers of 32 bytes each for input and tokens
  - local variables: money, prod_num, indexes ~ 16 bytes
  Total stack usage ~ 80 bytes max (mostly from buffers)
*/

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

/* Static buffers for input and parsing to avoid dynamic allocation */
static char input_buffer[INPUT_BUFFER_SIZE];   /* Input line buffer (max 31 chars + null) */
static char money_str[MONEY_STR_SIZE];         /* Money substring buffer */
static char product_str[PRODUCT_STR_SIZE];     /* Product number substring buffer */

static int32_t parse_int(const char * str, uint8_t len)
{
    /* Convert digit string to integer safely
       Return -1 if invalid (non-digit or empty) */
    int32_t val;
    uint8_t i;

    if (str == NULL)
    {
        return -1;
    }

    if (len == 0U)
    {
        return -1;
    }

    val = 0;
    for (i = 0U; i < len; i++)
    {
        char c;
        c = str[i];
        if ((c < '0') || (c > '9'))
        {
            return -1;
        }
        /* Prevent overflow for int (assuming 32-bit) */
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
    /* Utility: print fixed string with lcd.printf style (assuming simple %s)
       Emulate with lcd.setCursor + lcd.print */
    if (fmt == NULL)
    {
        return;
    }
    lcd.setCursor((int)col, (int)row);
    lcd.print(fmt);
    if (str != NULL)
    {
        lcd.print(str);
    }
}

static void lcd_print_int(uint8_t col, uint8_t row, const char * label, int32_t num)
{
    /* Prints label + number on LCD at specified position safely */
    char buf[LCD_COLS + 1U];
    int32_t n;

    if (label == NULL)
    {
        return;
    }

    lcd.setCursor((int)col, (int)row);
    n = snprintf(buf, (size_t)LCD_COLS + 1U, "%s%ld", label, (long)num);
    if (n > (int32_t)LCD_COLS)
    {
        n = (int32_t)LCD_COLS; /* truncate to 16 chars display width */
    }
    for (int32_t i = 0; i < n; i++)
    {
        lcd.print(buf[(size_t)i]);
    }
}

static void print_error_and_wait(const char * msg)
{
    /* Print error message on LCD and delay */
    lcd.clear();
    lcd.print(msg);
    delay(1500);
    lcd.clear();
}

void setup(void)
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    lcd.init();
    lcd.backlight();
    lcd.clear();

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

void loop(void)
{
    /* Single exit point variables */
    int32_t money;
    int32_t prod_num;
    uint8_t i;
    uint8_t j;
    const Product * selected;
    char * start;
    char * space_ptr;
    uint8_t money_len;
    uint8_t prod_len;
    char * prod_start;
    int32_t change;
    int32_t ret_status;
    size_t input_len;
    bool input_valid;

    if (Serial.available() == 0U)
    {
        /* No data */
        return;
    }

    /* Read up to 31 chars or until newline */
    i = 0U;
    while ((Serial.available() != 0U) && (i < (INPUT_BUFFER_SIZE - 1U)))
    {
        int c_int;
        char c_char;

        c_int = Serial.read();
        if (c_int == -1)
        {
            break; /* No data */
        }
        c_char = (char)c_int;
        if ((c_char == '\n') || (c_char == '\r'))
        {
            break;
        }
        input_buffer[i] = c_char;
        i++;
    }
    input_buffer[i] = '\0';
    input_len = (size_t)i;

    /* Trim leading spaces */
    start = input_buffer;
    while ((*start == ' ') && (*start != '\0'))
    {
        start++;
    }

    /* Find space separator between money and product number */
    space_ptr = NULL;
    for (j = 0U; start[j] != '\0'; j++)
    {
        if (start[j] == ' ')
        {
            space_ptr = &start[j];
            break;
        }
    }

    input_valid = true;

    if (space_ptr == NULL)
    {
        input_valid = false;
    }

    if (input_valid != false)
    {
        /* Extract money substring (trim trailing spaces) */
        uint8_t money_start_index = 0U;
        uint8_t money_end_index;

        money_start_index = 0U;
        money_end_index = (uint8_t)(space_ptr - start);

        while ((money_end_index > money_start_index) && (start[money_end_index - 1U] == ' '))
        {
            money_end_index--;
        }

        money_len = (uint8_t)(money_end_index - money_start_index);

        /* Extract product substring (skip spaces after separator) */
        prod_start = space_ptr + 1U;
        while ((*prod_start == ' ') && (*prod_start != '\0'))
        {
            prod_start++;
        }

        /* product string length */
        prod_len = 0U;
        while ((prod_start[prod_len] != '\0') && (prod_start[prod_len] != ' ') && (prod_len < (PRODUCT_STR_SIZE - 1U)))
        {
            prod_len++;
        }

        /* Check lengths within buffer limits */
        if ((money_len == 0U) || (money_len >= MONEY_STR_SIZE) || (prod_len == 0U) || (prod_len >= PRODUCT_STR_SIZE))
        {
            input_valid = false;
        }
        else
        {
            /* Copy money substring */
            for (j = 0U; j < money_len; j++)
            {
                money_str[j] = start[j];
            }
            money_str[money_len] = '\0';

            /* Copy product substring */
            for (j = 0U; j < prod_len; j++)
            {
                product_str[j] = prod_start[j];
            }
            product_str[prod_len] = '\0';

            /* Parse integers */
            money = parse_int(money_str, money_len);
            prod_num = parse_int(product_str, prod_len);

            if ((money < 0) || (prod_num < 1) || ((uint8_t)prod_num > product_count))
            {
                input_valid = false;
            }
        }
    }

    if (input_valid == false)
    {
        Serial.println("Invalid input format.");
        print_error_and_wait(ERR_INVALID_INPUT);
        /* Single exit: no return here */
    }
    else
    {
        selected = &products[(uint8_t)(prod_num - 1)];

        Serial.print("Selected: ");
        Serial.print(selected->name);
        Serial.print(" Price: ");
        Serial.print(selected->price);
        Serial.print(" Money: ");
        Serial.println(money);

        lcd.clear();

        if (money < selected->price)
        {
            Serial.println("Not enough money.");
            lcd.print(ERR_NOT_ENOUGH_MONEY);
            delay(1500);
            lcd.clear();
            /* Single exit */
        }
        else
        {
            /* Turn on LED to indicate product output for 2 seconds */
            digitalWrite(LED_PIN, HIGH);
            lcd.setCursor(0,0);
            {
                char line[LCD_COLS + 1U];
                /* "Output: " is 8 chars, leave 8 chars max for product name to fit 16 chars */
                /* Use precision specifier to limit name length */
                (void)snprintf(line, (size_t)(LCD_COLS + 1U), "Output: %.8s", selected->name);
                lcd.print(line);
            }
            delay(2000);
            digitalWrite(LED_PIN, LOW);

            change = money - selected->price;

            lcd.setCursor(0,1);
            {
                char line[LCD_COLS + 1U];
                (void)snprintf(line, (size_t)(LCD_COLS + 1U), "Change: %ld Won", (long)change);
                lcd.print(line);
            }
            Serial.print("Change returned: ");
            Serial.println(change);

            delay(3000);
            lcd.clear();
            Serial.println(MSG_ENTER);
        }
    }
}