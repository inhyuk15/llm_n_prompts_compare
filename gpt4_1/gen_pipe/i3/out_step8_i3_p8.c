#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <limits.h>
#include <assert.h>

#define kLedPin 10U /* LED pin for product output */
#define kLcdCols 16U
#define kLcdRows 2U

#define kInputBufferSize 32U
#define kMoneyStrSize 16U
#define kProductStrSize 16U

typedef enum {
  kErrNone = 0,
  kErrInvalidInputFormat,
  kErrInvalidMoneyValue,
  kErrInvalidProductValue,
  kErrProductNotFound,
  kErrNotEnoughMoney,
  kErrSerialReadFail,
  kErrLcdFail,
  kErrUnknown,
} error_code_t;

typedef struct {
  int32_t price;           /* Price of the product in currency units */
  const char *name;        /* Name of the product */
} product_t;

static const product_t kProducts[] = {
    {1500, "Cola"},
    {1200, "Water"},
    {1800, "Juice"},
    {1000, "Snack"},
};
static const uint8_t kProductCount = (uint8_t)(sizeof(kProducts) / sizeof(kProducts[0]));

/* I2C LCD instance (address 0x27, 16 columns, 2 rows) */
static LiquidCrystal_I2C lcd(0x27, kLcdCols, kLcdRows);

/* Volatile static buffers for input and parsing to avoid dynamic allocation */
/** Input line buffer: max 31 characters + null terminator */
static volatile char g_input_buffer[kInputBufferSize];
/** Buffer to hold parsed money substring */
static volatile char g_money_str[kMoneyStrSize];
/** Buffer to hold parsed product number substring */
static volatile char g_product_str[kProductStrSize];

/**
 * @brief Enter critical section by disabling interrupts.
 * @return Previous status register value to restore later.
 */
static inline uint8_t EnterCriticalSection(void) {
  uint8_t sreg = SREG;
  cli();
  return sreg;
}

/**
 * @brief Exit critical section by restoring interrupts.
 * @param sreg Previous status register value to restore.
 */
static inline void ExitCriticalSection(uint8_t sreg) {
  SREG = sreg;
}

/**
 * @brief Parses a decimal integer from given string with length check.
 *
 * @param str String containing digits to parse.
 * @param len Length of the substring to parse.
 * @param[out] out_value Pointer to store the parsed integer.
 * @return error_code_t indicating success or error type.
 */
static error_code_t ParseInt(const char *str, uint8_t len, int32_t *out_value) {
  if (str == NULL || out_value == NULL || len == 0 || len >= kMoneyStrSize) {
    return kErrInvalidInputFormat;
  }

  int32_t val = 0;
  for (uint8_t i = 0; i < len; i++) {
    char c = str[i];
    if (c < '0' || c > '9') {
      return kErrInvalidInputFormat;
    }
    if (val > ((INT_MAX - (int32_t)(c - '0')) / 10)) {
      return kErrInvalidInputFormat;
    }
    val = val * 10 + (int32_t)(c - '0');
  }

  *out_value = val;
  return kErrNone;
}

/**
 * @brief Prints formatted fixed string on LCD at specified position.
 *
 * @param col Column position (0-based).
 * @param row Row position (0-based).
 * @param fmt Constant string to print first.
 * @param str Optional string to print immediately after fmt.
 * @return error_code_t indicating success or failure.
 */
static error_code_t LcdPrintfFixed(uint8_t col, uint8_t row, const char *fmt, const char *str) {
  if (fmt == NULL) {
    return kErrInvalidInputFormat;
  }
  if (col >= kLcdCols || row >= kLcdRows) {
    return kErrLcdFail;
  }
  lcd.setCursor((int)col, (int)row);
  lcd.print(fmt);
  if (str != NULL) {
    lcd.print(str);
  }
  return kErrNone;
}

/**
 * @brief Prints a label and an integer number on the LCD at the specified position.
 *
 * @param col Column position (0-based).
 * @param row Row position (0-based).
 * @param label String label to print before the number.
 * @param num Integer number to print.
 * @return error_code_t indicating success or failure.
 */
static error_code_t LcdPrintInt(uint8_t col, uint8_t row, const char *label, int32_t num) {
  if (label == NULL) {
    return kErrInvalidInputFormat;
  }
  if (col >= kLcdCols || row >= kLcdRows) {
    return kErrLcdFail;
  }
  char buf[kLcdCols + 1U];
  lcd.setCursor((int)col, (int)row);
  int ret = snprintf(buf, (size_t)kLcdCols + 1U, "%s%ld", label, (long)num);
  if (ret < 0) {
    return kErrLcdFail;
  }
  lcd.print(buf);
  return kErrNone;
}

/**
 * @brief Delay with periodic yield calls to allow cooperative task switching.
 *
 * @param ms Number of milliseconds to delay.
 */
static void DelayWithYield(uint32_t ms) {
  uint32_t start = millis();
  while ((millis() - start) < ms) {
    delay(1);
    yield();
  }
}

/**
 * @brief Prints an error message on the LCD, waits for a short delay, and then clears the LCD.
 *
 * @param msg Message string to display, or NULL to display generic error.
 */
static void PrintErrorAndWait(const char *msg) {
  if (msg == NULL) {
    msg = "Error";
  }
  uint8_t sreg = EnterCriticalSection();
  lcd.clear();
  lcd.print(msg);
  ExitCriticalSection(sreg);
  DelayWithYield(1500);
  sreg = EnterCriticalSection();
  lcd.clear();
  ExitCriticalSection(sreg);
}

/**
 * @brief Reads a single input line from the Serial interface into the input buffer.
 *
 * @return kErrNone on success, kErrSerialReadFail if no input, otherwise error code.
 */
static error_code_t ReadSerialInput(void) {
  uint8_t i = 0U;

  if (Serial.available() == 0U) {
    return kErrSerialReadFail; /* No input available */
  }

  uint8_t sreg = EnterCriticalSection();

  while ((Serial.available() != 0U) && (i < (kInputBufferSize - 1U))) {
    int c_int = Serial.read();
    if (c_int == -1) {
      break;
    }
    char c_char = (char)c_int;
    if ((c_char == '\n') || (c_char == '\r')) {
      break;
    }
    ((char *)g_input_buffer)[i++] = c_char;
  }
  ((char *)g_input_buffer)[i] = '\0';

  ExitCriticalSection(sreg);

  if (i == 0) {
    return kErrSerialReadFail;
  }
  return kErrNone;
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
 * @return kErrNone on success, otherwise an error code.
 */
static error_code_t ExtractTokens(const char *input,
                                 char *money_out,
                                 uint8_t *money_len_out,
                                 char *product_out,
                                 uint8_t *product_len_out) {
  if (input == NULL || money_out == NULL || product_out == NULL) {
    return kErrInvalidInputFormat;
  }

  const char *start = input;
  const char *space_ptr = NULL;
  uint8_t j = 0;

  /* Trim leading spaces */
  while ((*start == ' ') && (*start != '\0')) {
    start++;
  }
  if (*start == '\0') {
    return kErrInvalidInputFormat;
  }

  /* Find space separator */
  for (j = 0U; start[j] != '\0'; j++) {
    if (start[j] == ' ') {
      space_ptr = &start[j];
      break;
    }
  }
  if (space_ptr == NULL) {
    return kErrInvalidInputFormat;
  }

  /* Extract money substring (trim trailing spaces) */
  uint8_t money_start_index = 0U;
  uint8_t money_end_index = (uint8_t)(space_ptr - start);

  while ((money_end_index > money_start_index) && (start[money_end_index - 1U] == ' ')) {
    money_end_index--;
  }

  uint8_t money_len = (uint8_t)(money_end_index - money_start_index);

  /* Extract product substring (skip spaces after separator) */
  const char *prod_start = space_ptr + 1U;
  while ((*prod_start == ' ') && (*prod_start != '\0')) {
    prod_start++;
  }

  uint8_t prod_len = 0U;
  while ((prod_start[prod_len] != '\0') && (prod_start[prod_len] != ' ') && (prod_len < (kProductStrSize - 1U))) {
    prod_len++;
  }

  /* Validate lengths */
  if ((money_len == 0U) || (money_len >= kMoneyStrSize) || (prod_len == 0U) || (prod_len >= kProductStrSize)) {
    return kErrInvalidInputFormat;
  }

  /* Copy money substring with boundary check */
  for (j = 0U; j < money_len && j < kMoneyStrSize - 1U; j++) {
    money_out[j] = start[j];
  }
  money_out[(j < kMoneyStrSize) ? j : (kMoneyStrSize - 1U)] = '\0';

  /* Copy product substring with boundary check */
  for (j = 0U; j < prod_len && j < kProductStrSize - 1U; j++) {
    product_out[j] = prod_start[j];
  }
  product_out[(j < kProductStrSize) ? j : (kProductStrSize - 1U)] = '\0';

  if (money_len_out != NULL) {
    *money_len_out = money_len;
  }
  if (product_len_out != NULL) {
    *product_len_out = prod_len;
  }
  return kErrNone;
}

/**
 * @brief Handles a valid transaction given money and product number.
 *
 * Checks if product number is valid, compares money to price, outputs product if enough money,
 * displays change, and handles error messages if applicable.
 *
 * @param money Amount of money inserted.
 * @param prod_num Product number selected.
 * @return error_code_t indicating outcome of transaction.
 */
static error_code_t HandleValidTransaction(int32_t money, int32_t prod_num) {
  if (prod_num < 1 || (uint8_t)prod_num > kProductCount) {
    return kErrProductNotFound;
  }

  uint8_t product_idx = (uint8_t)(prod_num - 1);
  const product_t *selected = &kProducts[product_idx];

  uint8_t sreg = EnterCriticalSection();

  Serial.print(F("Selected: "));
  Serial.print(selected->name != NULL ? selected->name : F("(null)"));
  Serial.print(F(" Price: "));
  Serial.print(selected->price);
  Serial.print(F(" Money: "));
  Serial.println(money);

  lcd.clear();

  if (money < selected->price) {
    Serial.println(F("Not enough money."));
    lcd.print(F("Not enough $"));
    ExitCriticalSection(sreg);

    DelayWithYield(1500);

    sreg = EnterCriticalSection();
    lcd.clear();
    ExitCriticalSection(sreg);
    return kErrNotEnoughMoney;
  } else {
    digitalWrite(kLedPin, HIGH);
    lcd.setCursor(0, 0);
    {
      char line[kLcdCols + 1U];
      int ret = snprintf(line, (size_t)(kLcdCols + 1U), "Output: %.8s", selected->name != NULL ? selected->name : "");
      if (ret < 0) {
        ExitCriticalSection(sreg);
        digitalWrite(kLedPin, LOW);
        return kErrLcdFail;
      }
      lcd.print(line);
    }
    ExitCriticalSection(sreg);

    DelayWithYield(2000);

    digitalWrite(kLedPin, LOW);

    int32_t change = 0;
    if (money >= selected->price) {
      change = money - selected->price;
    }

    sreg = EnterCriticalSection();
    lcd.setCursor(0, 1);
    {
      char line[kLcdCols + 1U];
      int ret = snprintf(line, (size_t)(kLcdCols + 1U), "Change: %ld Won", (long)change);
      if (ret < 0) {
        ExitCriticalSection(sreg);
        return kErrLcdFail;
      }
      lcd.print(line);
    }
    Serial.print(F("Change returned: "));
    Serial.println(change);

    ExitCriticalSection(sreg);

    DelayWithYield(3000);

    sreg = EnterCriticalSection();
    lcd.clear();
    Serial.println(F("Enter money and product number:"));
    ExitCriticalSection(sreg);

    return kErrNone;
  }
}

/**
 * @brief Processes the current line of input, parsing and executing transactions.
 *
 * Reads the shared input buffer, extracts tokens, validates inputs, and executes transactions.
 */
static void ProcessInputLine(void) {
  error_code_t err = kErrNone;
  int32_t money = -1;
  int32_t prod_num = -1;
  uint8_t money_len = 0, product_len = 0;
  char local_input_buffer[kInputBufferSize];
  char local_money_str[kMoneyStrSize];
  char local_product_str[kProductStrSize];

  uint8_t sreg = EnterCriticalSection();
  for (uint8_t i = 0; i < kInputBufferSize; i++) {
    local_input_buffer[i] = g_input_buffer[i];
  }
  ExitCriticalSection(sreg);

  err = ExtractTokens(local_input_buffer, local_money_str, &money_len, local_product_str, &product_len);
  if (err != kErrNone) {
    sreg = EnterCriticalSection();
    Serial.println(F("Invalid input format."));
    ExitCriticalSection(sreg);
    PrintErrorAndWait("Invalid input");
    return;
  }

  err = ParseInt(local_money_str, money_len, &money);
  if (err != kErrNone) {
    sreg = EnterCriticalSection();
    Serial.println(F("Invalid money value."));
    ExitCriticalSection(sreg);
    PrintErrorAndWait("Invalid input");
    return;
  }

  err = ParseInt(local_product_str, product_len, &prod_num);
  if (err != kErrNone || prod_num < 1 || (uint8_t)prod_num > kProductCount) {
    sreg = EnterCriticalSection();
    Serial.println(F("Invalid product number."));
    ExitCriticalSection(sreg);
    PrintErrorAndWait("Invalid input");
    return;
  }

  err = HandleValidTransaction(money, prod_num);
  if (err != kErrNone && err != kErrNotEnoughMoney) {
    sreg = EnterCriticalSection();
    Serial.println(F("Transaction failed."));
    ExitCriticalSection(sreg);
    PrintErrorAndWait("Error");
  }
}

/**
 * @brief Prints the welcome messages on LCD and Serial interface.
 *
 * @return error_code_t indicating success or failure.
 */
static error_code_t PrintWelcomeMessages(void) {
  uint8_t sreg = EnterCriticalSection();

  lcd.setCursor(0, 0);
  lcd.print(F("Vending Machine"));
  lcd.setCursor(0, 1);
  lcd.print(F("Insert money+num"));

  ExitCriticalSection(sreg);

  DelayWithYield(2000);

  sreg = EnterCriticalSection();
  lcd.clear();
  ExitCriticalSection(sreg);

  sreg = EnterCriticalSection();
  Serial.println(F("Enter money and product number:"));
  Serial.println(F("Format: <money> <product_num>"));
  Serial.println(F("Example: 2000 1"));
  ExitCriticalSection(sreg);

  return kErrNone;
}

/**
 * @brief Arduino setup function, initializes hardware and displays welcome messages.
 */
void setup(void) {
  Serial.begin(115200);

  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, LOW);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  PrintWelcomeMessages();
}

/**
 * @brief Arduino main loop function, reads input and processes transactions.
 */
void loop(void) {
  error_code_t err = ReadSerialInput();
  if (err == kErrNone) {
    ProcessInputLine();
  } else if (err == kErrSerialReadFail) {
    /* no input, just continue */
  } else {
    uint8_t sreg = EnterCriticalSection();
    Serial.println(F("Serial read error."));
    ExitCriticalSection(sreg);
    PrintErrorAndWait("Input error");
  }
}