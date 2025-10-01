#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define LED_PIN  10  // LED pin for product output (change if necessary)

/*
  stack usage estimates:
  - loop function uses fixed buffers of 32 bytes each for input and tokens
  - local variables: money, prod_num, indexes ~ 16 bytes
  Total stack usage ~ 80 bytes max (mostly from buffers)
*/

typedef struct {
  int price;
  const char *name;
} Product;

static const Product products[] = {
  {1500, "Cola"},
  {1200, "Water"},
  {1800, "Juice"},
  {1000, "Snack"}
};
static const int product_count = sizeof(products) / sizeof(products[0]);

static LiquidCrystal_I2C lcd(0x27, 16, 2); // I2C address 0x27, 16x2 LCD

// Static buffers for input and parsing to avoid dynamic allocation
static char input_buffer[32];   // Input line buffer (max 31 chars + null)
static char money_str[16];      // Money substring buffer
static char product_str[16];    // Product number substring buffer

static void lcd_printf_fixed(int col, int row, const char *fmt, const char *str) {
  // Utility: print fixed string with lcd.printf style (assuming simple %s)
  // Since lcd.printf is not standard, emulate with lcd.setCursor + lcd.print
  lcd.setCursor(col, row);
  lcd.print(fmt);
  lcd.print(str);
}

static void lcd_print_number(int col, int row, const char *label, int num) {
  // Prints label + number on LCD at specified position safely
  char buf[17];
  lcd.setCursor(col, row);
  int n = snprintf(buf, sizeof(buf), "%s%d", label, num);
  if (n > 16) n = 16; // truncate to 16 chars display width
  for (int i = 0; i < n; i++) {
    lcd.print(buf[i]);
  }
}

static int parse_int(const char *str, int len) {
  // Convert digit string to integer safely
  // Return -1 if invalid (non-digit or empty)
  if (len <= 0) return -1;
  int val = 0;
  for (int i = 0; i < len; i++) {
    char c = str[i];
    if (c < '0' || c > '9') return -1;
    // Prevent overflow for int (assuming 32-bit)
    if (val > (INT_MAX - (c - '0'))/10) return -1;
    val = val * 10 + (c - '0');
  }
  return val;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Vending Machine");
  lcd.setCursor(0, 1);
  lcd.print("Insert money+num");
  delay(2000);
  lcd.clear();

  Serial.println("Enter money and product number:");
  Serial.println("Format: <money> <product_num>");
  Serial.println("Example: 2000 1");
}

void loop() {
  if (Serial.available()) {
    // Read up to 31 chars or until newline
    size_t i = 0;
    while (Serial.available() && i < sizeof(input_buffer) - 1) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') break;
      input_buffer[i++] = c;
    }
    input_buffer[i] = '\0';

    // Trim leading spaces
    char *start = input_buffer;
    while (*start == ' ') start++;

    // Find space separator between money and product number
    char *space_ptr = NULL;
    for (size_t j = 0; start[j] != '\0'; j++) {
      if (start[j] == ' ') {
        space_ptr = &start[j];
        break;
      }
    }

    if (!space_ptr) {
      Serial.println("Invalid input format.");
      lcd.clear();
      lcd.print("Invalid input");
      delay(1500);
      lcd.clear();
      return;
    }

    // Extract money substring (trim trailing spaces)
    size_t money_len = space_ptr - start;
    while ((money_len > 0) && (start[money_len - 1] == ' '))
      money_len--;

    // Extract product substring (skip spaces after separator)
    char *prod_start = space_ptr + 1;
    while (*prod_start == ' ') prod_start++;
    size_t prod_len = 0;
    for (; prod_start[prod_len] != '\0'; prod_len++) {
      if (prod_start[prod_len] == ' ') break;
    }

    // Check lengths within buffer limits
    if (money_len == 0 || money_len >= sizeof(money_str) || prod_len == 0 || prod_len >= sizeof(product_str)) {
      Serial.println("Invalid input format.");
      lcd.clear();
      lcd.print("Invalid input");
      delay(1500);
      lcd.clear();
      return;
    }

    // Copy money substring
    memcpy(money_str, start, money_len);
    money_str[money_len] = '\0';

    // Copy product substring
    memcpy(product_str, prod_start, prod_len);
    product_str[prod_len] = '\0';

    int money = parse_int(money_str, money_len);
    int prod_num = parse_int(product_str, prod_len);

    if (money < 0 || prod_num < 1 || prod_num > product_count) {
      Serial.println("Invalid product number or money.");
      lcd.clear();
      lcd.print("Invalid input");
      delay(1500);
      lcd.clear();
      return;
    }

    const Product *selected = &products[prod_num - 1];
    Serial.print("Selected: ");
    Serial.print(selected->name);
    Serial.print(" Price: ");
    Serial.print(selected->price);
    Serial.print(" Money: ");
    Serial.println(money);

    lcd.clear();

    if (money < selected->price) {
      Serial.println("Not enough money.");
      lcd.print("Not enough $");
      delay(1500);
      lcd.clear();
      return;
    }

    // Turn on LED to indicate product output for 2 seconds
    digitalWrite(LED_PIN, HIGH);
    lcd.setCursor(0, 0);
    // Print "Output: name" safely with max 16 chars (16x2 LCD)
    {
      char line[17];
      int len = snprintf(line, sizeof(line), "Output: %.9s", selected->name);
      // "Output: " is 8 chars, leave 8 chars max for product name to fit 16 chars
      // Use precision specifier to limit name length
      lcd.print(line);
    }
    delay(2000);
    digitalWrite(LED_PIN, LOW);

    int change = money - selected->price;

    lcd.setCursor(0, 1);
    // Print "Change: NN Won" safely
    {
      char line[17];
      int len = snprintf(line, sizeof(line), "Change: %d Won", change);
      lcd.print(line);
    }
    Serial.print("Change returned: ");
    Serial.println(change);

    delay(3000);
    lcd.clear();
    Serial.println("Enter money and product number:");
  }
}