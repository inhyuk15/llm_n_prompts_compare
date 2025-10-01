#include <Arduino.h>
#include <stdint.h>
#include <string.h>

// Constants to replace magic numbers
#define INPUT_BUFFER_SIZE 32
#define THRESHOLD_PERCENTAGE 0.1F
#define FLASH_DURATION_MS 500U

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
  
  char inputBuffer[INPUT_BUFFER_SIZE] = {0};  // Fixed-size buffer
  int32_t i = 0;
  
  // Read input with explicit buffer bounds checking
  while (i < (int32_t)(sizeof(inputBuffer) - 1) && Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') break;
    inputBuffer[i++] = c;
  }
  inputBuffer[i] = '\0';
  
  // Find space character
  int32_t spaceIndex = -1;
  for (int32_t j = 0; j < i; j++) {
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

// Stack usage: ~20 bytes (float, uint32_t variables)
void loop() {
  float threshold = THRESHOLD_PERCENTAGE * stop_time;

  if (walk_time > threshold) {
    // Solid blue for (walk_time - threshold) seconds
    digitalWrite(bluePin, HIGH);
    delay((uint32_t)((walk_time - threshold) * 1000.0F));
    digitalWrite(bluePin, LOW);

    // Flashing green for threshold seconds
    uint32_t flashStartTime = millis();
    uint32_t flashDuration = (uint32_t)(threshold * 1000.0F);
    while (millis() - flashStartTime < flashDuration) {
      digitalWrite(greenPin, HIGH);
      delay(FLASH_DURATION_MS);
      digitalWrite(greenPin, LOW);
      delay(FLASH_DURATION_MS);
    }
  } else {
    // Solid blue for walk_time seconds
    digitalWrite(bluePin, HIGH);
    delay((uint32_t)(walk_time * 1000.0F));
    digitalWrite(bluePin, LOW);
  }

  // Red (stop) signal
  digitalWrite(redPin, HIGH);
  delay((uint32_t)(stop_time * 1000.0F));
  digitalWrite(redPin, LOW);
}