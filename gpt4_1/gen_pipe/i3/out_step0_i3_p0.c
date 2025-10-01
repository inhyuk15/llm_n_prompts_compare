#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define LED_PIN  10  // LED pin for product output (change if necessary)

LiquidCrystal_I2C lcd(0x27, 16, 2); // I2C address 0x27, 16x2 LCD

typedef struct {
  int price;
  const char *name;
} Product;

Product products[] = {
  {1500, "Cola"},
  {1200, "Water"},
  {1800, "Juice"},
  {1000, "Snack"}
};

const int product_count = sizeof(products) / sizeof(products[0]);

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
    int money = 0;
    int prod_num = 0;
    String input = Serial.readStringUntil('\n');
    input.trim();

    int spaceIndex = input.indexOf(' ');
    if (spaceIndex == -1) {
      Serial.println("Invalid input format.");
      lcd.clear();
      lcd.print("Invalid input");
      delay(1500);
      lcd.clear();
      return;
    }

    String money_str = input.substring(0, spaceIndex);
    String prod_str = input.substring(spaceIndex + 1);

    money = money_str.toInt();
    prod_num = prod_str.toInt();

    lcd.clear();

    if (prod_num < 1 || prod_num > product_count) {
      Serial.println("Invalid product number.");
      lcd.print("Invalid product");
      delay(1500);
      lcd.clear();
      return;
    }

    Product selected = products[prod_num - 1];
    Serial.printf("Selected: %s Price: %d Money: %d\n", selected.name, selected.price, money);

    if (money < selected.price) {
      Serial.println("Not enough money.");
      lcd.print("Not enough $");
      delay(1500);
      lcd.clear();
      return;
    }

    // Turn on LED to indicate product output for 2 seconds
    digitalWrite(LED_PIN, HIGH);
    lcd.setCursor(0, 0);
    lcd.printf("Output: %s", selected.name);
    delay(2000);
    digitalWrite(LED_PIN, LOW);

    int change = money - selected.price;

    lcd.setCursor(0, 1);
    lcd.printf("Change: %d Won", change);
    Serial.printf("Change returned: %d\n", change);

    delay(3000);
    lcd.clear();
    Serial.println("Enter money and product number:");
  }
}