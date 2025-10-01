#include <Arduino.h>

// Static global variables with file scope
static const int redPin = 2;
static const int bluePin = 4;
static const int greenPin = 5;
static float stop_time = 30.0;
static float walk_time = 5.0;

// Stack usage: ~36 bytes (32-byte buffer + local variables)
void setup() {
  Serial.begin(115200);
  pinMode(redPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  digitalWrite(redPin, LOW);
  digitalWrite(bluePin, LOW);
  digitalWrite(greenPin, LOW);

  // Read input values with buffer overflow protection
  Serial.println("Enter stop_time and walk_time (seconds, e.g., 30 5):");
  
  char inputBuffer[32] = {0};  // Fixed-size buffer
  int i = 0;
  
  // Read input with explicit buffer bounds checking
  while (i < sizeof(inputBuffer) - 1 && Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') break;
    inputBuffer[i++] = c;
  }
  inputBuffer[i] = '\0';
  
  // Find space character
  int spaceIndex = -1;
  for (int j = 0; j < i; j++) {
    if (inputBuffer[j] == ' ') {
      spaceIndex = j;
      break;
    }
  }
  
  if (spaceIndex != -1) {
    char *endptr;
    stop_time = strtof(inputBuffer, &endptr);
    walk_time = strtof(inputBuffer + spaceIndex + 1, &endptr);
  }
}

// Stack usage: ~20 bytes (float, unsigned long variables)
void loop() {
  float threshold = 0.1 * stop_time;

  if (walk_time > threshold) {
    // Solid blue for (walk_time - threshold) seconds
    digitalWrite(bluePin, HIGH);
    delay((unsigned long)((walk_time - threshold) * 1000));
    digitalWrite(bluePin, LOW);

    // Flashing green for threshold seconds
    unsigned long flashStartTime = millis();
    unsigned long flashDuration = (unsigned long)(threshold * 1000);
    while (millis() - flashStartTime < flashDuration) {
      digitalWrite(greenPin, HIGH);
      delay(500);
      digitalWrite(greenPin, LOW);
      delay(500);
    }
  } else {
    // Solid blue for walk_time seconds
    digitalWrite(bluePin, HIGH);
    delay((unsigned long)(walk_time * 1000));
    digitalWrite(bluePin, LOW);
  }

  // Red (stop) signal
  digitalWrite(redPin, HIGH);
  delay((unsigned long)(stop_time * 1000));
  digitalWrite(redPin, LOW);
}