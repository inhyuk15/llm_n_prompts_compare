#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

// Error code definitions
typedef enum {
    SUCCESS = 0,
    INVALID_ARGUMENT,
    PARSING_FAILURE,
    OUT_OF_RANGE,
    MEMORY_ERROR,
    UNKNOWN_ERROR
} ErrorCode;

// Constants to replace magic numbers
#define INPUT_BUFFER_SIZE 32
#define THRESHOLD_PERCENTAGE 0.1F
#define FLASH_DURATION_MS 500U

// Static global variables with file scope
static const int redPin = 2;
static const int bluePin = 4;
static const int greenPin = 5;
static volatile float stop_time = 30.0;
static volatile float walk_time = 5.0;

// Initialize pins and set default states
ErrorCode setupPins() {
    if (redPin < 0 || bluePin < 0 || greenPin < 0) {
        return INVALID_ARGUMENT;
    }
    pinMode(redPin, OUTPUT);
    pinMode(bluePin, OUTPUT);
    pinMode(greenPin, OUTPUT);
    digitalWrite(redPin, LOW);
    digitalWrite(bluePin, LOW);
    digitalWrite(greenPin, LOW);
    return SUCCESS;
}

// Read input from serial with buffer overflow protection
ErrorCode readInput(char* buffer, size_t bufferSize) {
    if (buffer == NULL || bufferSize == 0) {
        return INVALID_ARGUMENT;
    }
    int32_t i = 0;
    while (i < (int32_t)(bufferSize - 1) && Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') break;
        buffer[i++] = c;
    }
    buffer[i] = '\0';
    return SUCCESS;
}

// Parse input buffer and update timing values
ErrorCode parseInput(const char* buffer) {
    if (buffer == NULL) {
        return INVALID_ARGUMENT;
    }
    int32_t spaceIndex = -1;
    size_t len = strlen(buffer);
    for (size_t j = 0; j < len; j++) {
        if (buffer[j] == ' ') {
            spaceIndex = j;
            break;
        }
    }

    if (spaceIndex != -1) {
        char *endptr1;
        float new_stop = strtof(buffer, &endptr1);
        if (endptr1 == buffer || spaceIndex != (endptr1 - buffer)) {
            return PARSING_FAILURE;
        }

        char *endptr2;
        float new_walk = strtof(buffer + spaceIndex + 1, &endptr2);
        if (endptr2 == (buffer + spaceIndex + 1) || *endptr2 != '\0') {
            return PARSING_FAILURE;
        }

        if (new_stop <= 0.0f || new_walk <= 0.0f) {
            return OUT_OF_RANGE;
        }

        noInterrupts();
        stop_time = new_stop;
        walk_time = new_walk;
        interrupts();
        return SUCCESS;
    } else {
        return PARSING_FAILURE; // No space found
    }
}

// Setup function
void setup() {
    Serial.begin(115200);
    ErrorCode ec = setupPins();
    if (ec != SUCCESS) {
        Serial.println("Failed to initialize pins. Using default values.");
        stop_time = 30.0f;
        walk_time = 5.0f;
    } else {
        Serial.println("Enter stop_time and walk_time (seconds, e.g., 30 5):");
        char inputBuffer[INPUT_BUFFER_SIZE] = {0};
        ec = readInput(inputBuffer, INPUT_BUFFER_SIZE);
        if (ec == SUCCESS) {
            ec = parseInput(inputBuffer);
            if (ec != SUCCESS) {
                Serial.println("Invalid input. Using default values.");
                stop_time = 30.0f;
                walk_time = 5.0f;
            }
        } else {
            Serial.println("Failed to read input. Using default values.");
            stop_time = 30.0f;
            walk_time = 5.0f;
        }
    }
}

// Flash LED with specified duration
ErrorCode flashLED(int pin, uint32_t duration) {
    if (pin < 0 || duration == 0) {
        return INVALID_ARGUMENT;
    }

    uint32_t startTime = millis();
    while (millis() - startTime < duration) {
        digitalWrite(pin, HIGH);
        delay(FLASH_DURATION_MS);
        digitalWrite(pin, LOW);
        delay(FLASH_DURATION_MS);
    }
    return SUCCESS;
}

// Set solid LED with specified duration
ErrorCode setSolidLED(int pin, float duration) {
    if (pin < 0 || duration < 0.0f) {
        return INVALID_ARGUMENT;
    }

    if (duration * 1000.0F > (float)UINT32_MAX) {
        duration = (float)UINT32_MAX / 1000.0F;
    }
    uint32_t delayMillis = (uint32_t)(duration * 1000.0F);
    digitalWrite(pin, HIGH);
    delay(delayMillis);
    digitalWrite(pin, LOW);
    return SUCCESS;
}

// Main loop function
void loop() {
    // Read volatile variables into local variables with critical section
    float st, wt;
    noInterrupts();
    st = stop_time;
    wt = walk_time;
    interrupts();

    if (st <= 0.0f) return;

    float threshold = THRESHOLD_PERCENTAGE * st;
    if (threshold * 1000.0F > (float)UINT32_MAX) {
        threshold = (float)UINT32_MAX / 1000.0F;
    }

    if (wt > threshold) {
        float durationBlue = wt - threshold;
        if (durationBlue <= 0.0f) return;
        ErrorCode ec = setSolidLED(bluePin, durationBlue);
        if (ec != SUCCESS) {
            Serial.println("Failed to set blue LED.");
        }

        uint32_t thresholdMillis = (uint32_t)(threshold * 1000.0F);
        ec = flashLED(greenPin, thresholdMillis);
        if (ec != SUCCESS) {
            Serial.println("Failed to flash green LED.");
        }
    } else {
        ec = setSolidLED(bluePin, wt);
        if (ec != SUCCESS) {
            Serial.println("Failed to set blue LED.");
        }
    }

    if (st * 1000.0F > (float)UINT32_MAX) return;
    uint32_t stopMillis = (uint32_t)(st * 1000.0F);
    ec = setSolidLED(redPin, st);
    if (ec != SUCCESS) {
        Serial.println("Failed to set red LED.");
    }
}