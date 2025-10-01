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

// Initialize pins and set default states
void setupPins() {
  pinMode(redPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  digitalWrite(redPin, LOW);
  digitalWrite(bluePin, LOW);
  digitalWrite(greenPin, LOW);
}

// Read input from serial with buffer overflow protection
void readInput(char* buffer, size_t bufferSize) {
  int32_t i = 0;
  while (i < (int32_t)(bufferSize - 1) && Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') break;
    buffer[i++] = c;
  }
  buffer[i] = '\0';
}

// Parse input buffer and update timing values
void parseInput(const char* buffer) {
  int32_t spaceIndex = -1;
  for (int32_t j = 0; j < strlen(buffer); j++) {
    if (buffer[j] == ' ') {
      spaceIndex = j;
      break;
    }
  }
  
  if (spaceIndex != -1) {
    char *endptr;
    stop_time = strtof(buffer, &endptr);
    walk_time = strtof(buffer + spaceIndex + 1, &endptr);
  }
}

// Setup function
void setup() {
  Serial.begin(115200);
  setupPins();
  Serial.println("Enter stop_time and walk_time (seconds, e.g., 30 5):");
  
  char inputBuffer[INPUT_BUFFER_SIZE] = {0};
  readInput(inputBuffer, INPUT_BUFFER_SIZE);
  parseInput(inputBuffer);
}

// Flash LED with specified duration
void flashLED(int pin, uint32_t duration) {
  uint32_t startTime = millis();
  while (millis() - startTime < duration) {
    digitalWrite(pin, HIGH);
    delay(FLASH_DURATION_MS);
    digitalWrite(pin, LOW);
    delay(FLASH_DURATION_MS);
  }
}

// Set solid LED with specified duration
void setSolidLED(int pin, float duration) {
  digitalWrite(pin, HIGH);
  delay((uint32_t)(duration * 1000.0F));
  digitalWrite(pin, LOW);
}

// Main loop function
void loop() {
  float threshold = THRESHOLD_PERCENTAGE * stop_time;

  if (walk_time > threshold) {
    // Solid blue for (walk_time - threshold) seconds
    setSolidLED(bluePin, walk_time - threshold);
    
    // Flashing green for threshold seconds
    flashLED(greenPin, (uint32_t)(threshold * 1000.0F));
  } else {
    // Solid blue for walk_time seconds
    setSolidLED(bluePin, walk_time);
  }

  // Red (stop) signal
  setSolidLED(redPin, stop_time);
}