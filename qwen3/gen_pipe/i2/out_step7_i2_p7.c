#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

/**
 * @brief Error codes returned by API functions
 */
typedef enum {
    SUCCESS = 0,          ///< Operation completed successfully
    INVALID_ARGUMENT,     ///< Invalid parameter passed to function
    PARSING_FAILURE,      ///< Failed to parse input string
    OUT_OF_RANGE,         ///< Value outside acceptable range
    MEMORY_ERROR,         ///< Memory allocation failure
    UNKNOWN_ERROR         ///< Unspecified error condition
} ErrorCode;

/**
 * @brief Constants used throughout the application
 */
#define INPUT_BUFFER_SIZE 32         ///< Maximum length of input buffer
#define THRESHOLD_PERCENTAGE 0.1F    ///< Percentage threshold for timing calculation
#define FLASH_DURATION_MS 500U       ///< Duration of single LED flash in milliseconds

/**
 * @brief LED pin assignments with file scope
 */
static const int redPin = 2;         ///< Red LED connected to pin 2
static const int bluePin = 4;        ///< Blue LED connected to pin 4
static const int greenPin = 5;       ///< Green LED connected to pin 5

/**
 * @brief Timing parameters with volatile access
 */
static volatile float stop_time = 30.0;  ///< Duration for stop phase in seconds
static volatile float walk_time = 5.0;   ///< Duration for walk phase in seconds

/**
 * @brief Initialize LED pins and set default states
 * 
 * @return SUCCESS if initialization completed, INVALID_ARGUMENT if any pin is invalid
 */
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

/**
 * @brief Read input from serial port with buffer overflow protection
 * 
 * @param buffer Output buffer to store input characters
 * @param bufferSize Size of the output buffer
 * @return SUCCESS if input read successfully, INVALID_ARGUMENT if parameters invalid
 */
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

/**
 * @brief Parse input string to update timing parameters
 * 
 * @param buffer Input string containing two space-separated floats
 * @return SUCCESS if parsing and update successful, PARSING_FAILURE for format errors, 
 *         OUT_OF_RANGE for invalid values, INVALID_ARGUMENT if buffer is null
 */
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

/**
 * @brief Arduino setup function for initialization
 * 
 * Initializes serial communication, LED pins, and processes user input
 */
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

/**
 * @brief Flash an LED for a specified duration
 * 
 * @param pin Target LED pin number
 * @param duration Total duration of flashing in milliseconds
 * @return SUCCESS if operation completed, INVALID_ARGUMENT if parameters invalid
 */
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

/**
 * @brief Set an LED to solid state for a specified duration
 * 
 * @param pin Target LED pin number
 * @param duration Duration to keep LED on in seconds
 * @return SUCCESS if operation completed, INVALID_ARGUMENT if parameters invalid
 */
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

/**
 * @brief Main application loop controlling LED behavior
 * 
 * Implements traffic light timing logic based on configured parameters
 */
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