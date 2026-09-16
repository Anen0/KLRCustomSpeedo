#include <Arduino.h>
#include <LCDWIKI_GUI.h>
#include <LCDWIKI_SPI.h>


#define SPEED_SENSOR_PIN 6

uint32_t pulseCount = 0;
uint32_t lastPulseCount = 0;
uint32_t lastReport = 0;

bool lastState = HIGH;

void setup() {
    Serial.begin(115200);

    pinMode(SPEED_SENSOR_PIN, INPUT_PULLUP);

    delay(2000);

    Serial.println();
    Serial.println("=== SPEED SENSOR TEST ===");
    Serial.println();
}

void loop() {

    bool currentState = digitalRead(SPEED_SENSOR_PIN);

    if (currentState != lastState) {

        if (currentState == LOW) {
            pulseCount++;
        }

        lastState = currentState;
    }

    if (millis() - lastReport >= 1000) {

        lastReport = millis();

        uint32_t pulsesThisSecond = pulseCount - lastPulseCount;
        lastPulseCount = pulseCount;

        Serial.print("Total: ");
        Serial.print(pulseCount);

        Serial.print(" | Pulses/sec: ");
        Serial.print(pulsesThisSecond);

        Serial.print(" | D6: ");
        Serial.println(currentState ? "HIGH" : "LOW");
    }
}