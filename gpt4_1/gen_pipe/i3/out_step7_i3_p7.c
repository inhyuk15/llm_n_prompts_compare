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

/**
 * @enum ErrorCode_t
 * @brief Error codes used throughout the vending machine program.
 */
typedef enum {
    ERR_NONE = 0,             /**< No error */
    ERR_INVALID_INPUT_FORMAT, /**< Input format is invalid */
    ERR_INVALID_MONEY_VALUE,  /**< Money value is invalid */
    ERR_INVALID_PRODUCT_VALUE,/**< Product value is invalid */
    ERR_PRODUCT_NOT_FOUND,    /**< Product number does not exist */
    ERR_NOT_ENOUGH_MONEY,     /**< Insufficient money for the product */
    ERR_SERIAL_READ_FAIL,     /**< Serial reading failure */
    ERR_LCD_FAIL,             /**< LCD operation failure */
    ERR_UNKNOWN,              /**< Unknown error */
} ErrorCode_t;

/**
 * @struct Product
 * @brief Represents a product with price and name.
 */
typedef struct
{
    int32_t price;      /**< Price of the product in currency units */
    const char * name;  /**< Name of the product */
} Product;

/* Product list */
static const Product products[] =
{
    {1500, "Cola"},
    {1200, "Water"},
    {1800, "Juice"},
    {1000, "Snack"}
};
static const uint8_t product_count = (uint8_t)(sizeof(products) / sizeof(products[0]));


/* I2C LCD instance (address 0x27, 16 columns, 2 rows) */
static LiquidCrystal_I2C lcd(0x27, LCD_COLS, LCD_ROWS);

/* Volatile static buffers for input and parsing to avoid dynamic allocation */
/** Input line buffer: max 31 characters + null terminator */
static volatile char input_buffer[INPUT_BUFFER_SIZE];
/** Buffer to hold parsed money substring */
static volatile char money_str[MONEY_STR_SIZE];
/** Buffer to hold parsed product number substring */
static volatile char product_str[PRODUCT_STR_SIZE];

/**
 * @brief Enter critical section by disabling interrupts.
 * @return Previous status register value to restore later.
 */
static inline uint8_t enter_critical_section(void)
{
    uint8_t sreg = SREG;
    cli();
    return sreg;
}

/**
 * @brief Exit critical section by restoring interrupts.
 * @param sreg Previous status register value to restore.
 */
static inline void exit_critical_section(uint8_t sreg)
{
    SREG = sreg;
}

/**
 * @brief Parses a decimal integer from given string with length check.
 *
 * @param str String containing digits to parse.
 * @param len Length of the substring to parse.
 * @param[out] out_value Pointer to store the parsed integer.
 * @return Error code indicating success or error type.
 */
static ErrorCode_t parse_int(const char * str, uint8_t len, int32_t* out_value)
{
    if (str == NULL || out_value == NULL || len == 0U || len >= MONEY_STR_SIZE)
    {
        return ERR_INVALID_INPUT_FORMAT;
    }

    int32_t val = 0;
    for (uint8_t i = 0U; i < len; i++)
    {
        char c = str[i];
        if ((c < '0') || (c > '9'))
        {
            return ERR_INVALID_INPUT_FORMAT;
        }
        if (val > ((INT_MAX - (int32_t)(c - '0')) / 10))
        {
            return ERR_INVALID_INPUT_FORMAT;
        }
        val = val * 10 + (int32_t)(c - '0');
    }

    *out_value = val;
    return ERR_NONE;
}

/**
 * @brief Prints formatted fixed string on LCD at specified position.
 *
 * @param col Column position (0-based).
 * @param row Row position (0-based).
 * @param fmt Constant string to print first.
 * @param str Optional string to print immediately after fmt.
 * @return Error code indicating success or failure.
 */
static ErrorCode_t lcd_printf_fixed(uint8_t col, uint8_t row, const char * fmt, const char * str)
{
    if (fmt == NULL)
    {
        return ERR_INVALID_INPUT_FORMAT;
    }
    if (col >= LCD_COLS || row >= LCD_ROWS)
    {
        return ERR_LCD_FAIL;
    }
    lcd.setCursor((int)col, (int)row);
    lcd.print(fmt);
    if (str != NULL)
    {
        lcd.print(str);
    }
    return ERR_NONE;
}

/**
 * @brief Prints a label and an integer number on the LCD at the specified position.
 *
 * @param col Column position (0-based).
 * @param row Row position (0-based).
 * @param label String label to print before the number.
 * @param num Integer number to print.
 * @return Error code indicating success or failure.
 */
static ErrorCode_t lcd_print_int(uint8_t col, uint8_t row, const char * label, int32_t num)
{
    if (label == NULL)
    {
        return ERR_INVALID_INPUT_FORMAT;
    }
    if (col >= LCD_COLS || row >= LCD_ROWS)
    {
        return ERR_LCD_FAIL;
    }
    char buf[LCD_COLS + 1U];
    lcd.setCursor((int)col, (int)row);
    int ret = snprintf(buf, (size_t)LCD_COLS + 1U, "%s%ld", label, (long)num);
    if (ret < 0)
    {
        return ERR_LCD_FAIL;
    }
    lcd.print(buf);
    return ERR_NONE;
}

/**
 * @brief Delay with periodic yield calls to allow cooperative task switching.
 *
 * @param ms Number of milliseconds to delay.
 */
static void delay_with_yield(uint32_t ms)
{
    /* A helper delay that allows periodic yielding for watchdog safety, if necessary */
    uint32_t start = millis();
    while ((millis() - start) < ms)
    {
        delay(1);
        yield();
    }
}

/**
 * @brief Prints an error message on the LCD, waits for a short delay, and then clears the LCD.
 *
 * @param msg Message string to display, or NULL to display generic error.
 */
static void print_error_and_wait(const char * msg)
{
    if (msg == NULL)
    {
        msg = "Error";
    }
    uint8_t sreg = enter_critical_section();
    lcd.clear();
    lcd.print(msg);
    exit_critical_section(sreg);
    delay_with_yield(1500);
    sreg = enter_critical_section();
    lcd.clear();
    exit_critical_section(sreg);
}

/**
 * @brief Reads a single input line from the Serial interface into the input buffer.
 *
 * @return ERR_NONE on success, ERR_SERIAL_READ_FAIL if no input, otherwise error code.
 */
static ErrorCode_t read_serial_input(void)
{
    uint8_t i = 0U;

    if (Serial.available() == 0U)
    {
        return ERR_SERIAL_READ_FAIL; /* No input available */
    }

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
        ((char*)input_buffer)[i++] = c_char;
    }
    ((char*)input_buffer)[i] = '\0';

    exit_critical_section(_sreg);

    if (i == 0)
    {
        return ERR_SERIAL_READ_FAIL;
    }
    return ERR_NONE;
}

/**
 * @brief Extracts money and product tokens from the input string.
 *
 * Splits input into two non-empty substrings separated by space(s), trimming leading/trailing spaces.
 *
 * @param input Input string to parse.
 * @param[out] money_out Buffer to store the extracted money substring.
 * @param[out] money_len_out Length of the money substring extracted.
 * @param[out] product_out Buffer to store the extracted product substring.
 * @param[out] product_len_out Length of the product substring extracted.
 * @return ERR_NONE on success, otherwise an error code.
 */
static ErrorCode_t extract_tokens(const char * input, char * money_out, uint8_t * money_len_out,
                           char * product_out, uint8_t * product_len_out)
{
    if (input == NULL || money_out == NULL || product_out == NULL)
    {
        return ERR_INVALID_INPUT_FORMAT;
    }

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
        return ERR_INVALID_INPUT_FORMAT;
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
        return ERR_INVALID_INPUT_FORMAT;
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
        return ERR_INVALID_INPUT_FORMAT;
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

    if (money_len_out != NULL)
    {
        *money_len_out = money_len;
    }
    if (product_len_out != NULL)
    {
        *product_len_out = prod_len;
    }
    return ERR_NONE;
}

/**
 * @brief Handles a valid transaction given money and product number.
 *
 * Checks if product number is valid, compares money to price, outputs product if enough money,
 * displays change, and handles error messages if applicable.
 *
 * @param money Amount of money inserted.
 * @param prod_num Product number selected.
 * @return Error code indicating outcome of transaction.
 */
static ErrorCode_t handle_valid_transaction(int32_t money, int32_t prod_num)
{
    if (prod_num < 1 || (uint8_t)prod_num > product_count)
    {
        return ERR_PRODUCT_NOT_FOUND;
    }

    uint8_t product_idx = (uint8_t)(prod_num - 1);
    const Product * selected = &products[product_idx];

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
        lcd.print("Not enough $");
        exit_critical_section(sreg);

        delay_with_yield(1500);

        sreg = enter_critical_section();
        lcd.clear();
        exit_critical_section(sreg);
        return ERR_NOT_ENOUGH_MONEY;
    }
    else
    {
        digitalWrite(LED_PIN, HIGH);
        lcd.setCursor(0, 0);
        {
            char line[LCD_COLS + 1U];
            int ret = snprintf(line, (size_t)(LCD_COLS + 1U), "Output: %.8s", selected->name != NULL ? selected->name : "");
            if (ret < 0)
            {
                exit_critical_section(sreg);
                digitalWrite(LED_PIN, LOW);
                return ERR_LCD_FAIL;
            }
            lcd.print(line);
        }
        exit_critical_section(sreg);

        delay_with_yield(2000);

        digitalWrite(LED_PIN, LOW);

        int32_t change = 0;
        if (money >= selected->price)
        {
            change = money - selected->price;
        }

        sreg = enter_critical_section();
        lcd.setCursor(0, 1);
        {
            char line[LCD_COLS + 1U];
            int ret = snprintf(line, (size_t)(LCD_COLS + 1U), "Change: %ld Won", (long)change);
            if (ret < 0)
            {
                exit_critical_section(sreg);
                return ERR_LCD_FAIL;
            }
            lcd.print(line);
        }
        Serial.print("Change returned: ");
        Serial.println(change);

        exit_critical_section(sreg);

        delay_with_yield(3000);

        sreg = enter_critical_section();
        lcd.clear();
        Serial.println("Enter money and product number:");
        exit_critical_section(sreg);

        return ERR_NONE;
    }
}

/**
 * @brief Processes the current line of input, parsing and executing transactions.
 *
 * Reads the shared input buffer, extracts tokens, validates inputs, and executes transactions.
 */
static void process_input_line(void)
{
    ErrorCode_t err = ERR_NONE;
    int32_t money = -1;
    int32_t prod_num = -1;
    uint8_t money_len = 0U, product_len = 0U;
    char local_input_buffer[INPUT_BUFFER_SIZE];
    char local_money_str[MONEY_STR_SIZE];
    char local_product_str[PRODUCT_STR_SIZE];

    /* Copy volatile input_buffer into local buffer atomically */
    uint8_t sreg = enter_critical_section();
    for(uint8_t i = 0; i < INPUT_BUFFER_SIZE; i++) {
        local_input_buffer[i] = input_buffer[i];
    }
    exit_critical_section(sreg);

    err = extract_tokens(local_input_buffer, local_money_str, &money_len, local_product_str, &product_len);
    if (err != ERR_NONE)
    {
        sreg = enter_critical_section();
        Serial.println("Invalid input format.");
        exit_critical_section(sreg);
        print_error_and_wait("Invalid input");
        return;
    }

    err = parse_int(local_money_str, money_len, &money);
    if (err != ERR_NONE)
    {
        sreg = enter_critical_section();
        Serial.println("Invalid money value.");
        exit_critical_section(sreg);
        print_error_and_wait("Invalid input");
        return;
    }

    err = parse_int(local_product_str, product_len, &prod_num);
    if (err != ERR_NONE || prod_num < 1 || (uint8_t)prod_num > product_count)
    {
        sreg = enter_critical_section();
        Serial.println("Invalid product number.");
        exit_critical_section(sreg);
        print_error_and_wait("Invalid input");
        return;
    }

    err = handle_valid_transaction(money, prod_num);
    if (err != ERR_NONE && err != ERR_NOT_ENOUGH_MONEY)
    {
        sreg = enter_critical_section();
        Serial.println("Transaction failed.");
        exit_critical_section(sreg);
        print_error_and_wait("Error");
    }
}

/**
 * @brief Prints the welcome messages on LCD and Serial interface.
 *
 * @return Error code indicating success or failure.
 */
static ErrorCode_t print_welcome_messages(void)
{
    uint8_t sreg = enter_critical_section();

    lcd.setCursor(0, 0);
    lcd.print("Vending Machine");
    lcd.setCursor(0, 1);
    lcd.print("Insert money+num");

    exit_critical_section(sreg);

    delay_with_yield(2000);

    sreg = enter_critical_section();
    lcd.clear();
    exit_critical_section(sreg);

    sreg = enter_critical_section();
    Serial.println("Enter money and product number:");
    Serial.println("Format: <money> <product_num>");
    Serial.println("Example: 2000 1");
    exit_critical_section(sreg);

    return ERR_NONE;
}

/**
 * @brief Arduino setup function, initializes hardware and displays welcome messages.
 */
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

/**
 * @brief Arduino main loop function, reads input and processes transactions.
 */
void loop(void)
{
    ErrorCode_t err = read_serial_input();
    if (err == ERR_NONE)
    {
        process_input_line();
    }
    else if (err == ERR_SERIAL_READ_FAIL)
    {
        /* no input, just continue */
    }
    else
    {
        uint8_t sreg = enter_critical_section();
        Serial.println("Serial read error.");
        exit_critical_section(sreg);
        print_error_and_wait("Input error");
    }
}