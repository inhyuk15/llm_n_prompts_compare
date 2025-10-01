#include <Arduino.h>

const int redPin = 2;
const int bluePin = 4;
const int greenPin = 5;

float stop_time = 30.0;
float walk_time = 5.0;

void setup() {
  Serial.begin(115200);
  pinMode(redPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  digitalWrite(redPin, LOW);
  digitalWrite(bluePin, LOW);
  digitalWrite(greenPin, LOW);

  // Read input values
  Serial.println("Enter stop_time and walk_time (seconds, e.g., 30 5):");
  while (!Serial.available()) {}
  String input = Serial.readStringUntil('\n');
  int spaceIndex = input.indexOf(' ');
  if (spaceIndex != -1) {
    String stopStr = input.substring(0, spaceIndex);
    String walkStr = input.substring(spaceIndex + 1);
    stop_time = stopStr.toFloat();
    walk_time = walkStr.toFloat();
  }
}

void loop() {
  float threshold = 0.1 * stop_time;

  if (walk_time > threshold) {
    // Solid blue for (walk_time - threshold) seconds
    digitalWrite(bluePin, HIGH);
    delay((walk_time - threshold) * 1000);
    digitalWrite(bluePin, LOW);

    // Flashing green for threshold seconds
    unsigned long flashStartTime = millis();
    unsigned long flashDuration = threshold * 1000;
    while (millis() - flashStartTime < flashDuration) {
      digitalWrite(greenPin, HIGH);
      delay(500);
      digitalWrite(greenPin, LOW);
      delay(500);
    }
  } else {
    // Solid blue for walk_time seconds
    digitalWrite(bluePin, HIGH);
    delay(walk_time * 1000);
    digitalWrite(bluePin, LOW);
  }

  // Red (stop) signal
  digitalWrite(redPin, HIGH);
  delay(stop_time * 1000);
  digitalWrite(redPin, LOW);
}