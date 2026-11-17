/*
 * ==============================================================================
 * TEST 04: 4x TACTILE PUSH BUTTONS TEST
 * ==============================================================================
 * HARDWARE PINOUT / WIRING GUIDE:
 * 
 * Each push button has 2 active pins/terminals:
 *  - One terminal connects to the ESP32-S3 GPIO pin.
 *  - The opposite diagonal terminal connects to common GND on the breadboard.
 * 
 * ESP32-S3 DevKit Pin   <---->   Button Function
 * -------------------------------------------------------------
 *  GPIO 7               <---->   Button 1: RECORD (Tap to trigger 5s WAV save)
 *  GPIO 2               <---->   Button 2: MIX TOGGLE (Hold to toggle Stereo/Mix)
 *  GPIO 3               <---->   Button 3: OLED TOGGLE (Hold to turn OLED On/Off)
 *  GPIO 1               <---->   Button 4: CH2 HET (Hold to tune Ch2 carrier)
 *  GND                  <---->   Common ground for all 4 buttons
 * 
 * Note: Internal pull-ups (INPUT_PULLUP) are enabled in code. 
 * An unpressed button reads HIGH (3.3V).
 * When pressed, the button connects the pin to GND and reads LOW (0V).
 * 
 * SERIAL MONITOR: 115200 Baud
 * ==============================================================================
 */

#include <Arduino.h>

struct ButtonDef {
  uint8_t pin;
  const char* name;
  const char* role;
  bool lastState;
  unsigned long pressStartTime;
  uint32_t pressCount;
  bool longPressFired;
};

ButtonDef buttons[] = {
  { 7, "RECORD",      "Save last 5s audio buffer", HIGH, 0, 0, false },
  { 2, "MIX_TOGGLE",  "Switch Stereo / Mixed Mono", HIGH, 0, 0, false },
  { 3, "OLED_TOGGLE", "Turn OLED screen On / Off",  HIGH, 0, 0, false },
  { 1, "CH2_HET",     "Adjust Channel 2 Carrier",   HIGH, 0, 0, false }
};

const int NUM_BUTTONS = sizeof(buttons) / sizeof(buttons[0]);
const unsigned long LONG_PRESS_MS = 500; // 0.5s threshold matching Esperdyne firmware

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=======================================================");
  Serial.println("   ESPERDYNE BAT DETECTOR - TEST 04: TACTILE BUTTONS   ");
  Serial.println("=======================================================");
  Serial.println("Instructions:");
  Serial.println("Press each tactile button on your breadboard one by one.");
  Serial.println("Test both quick taps (<500ms) and long presses (>500ms).");
  Serial.println("=======================================================\n");

  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    buttons[i].lastState = digitalRead(buttons[i].pin);
  }

  Serial.println("Button Configuration:");
  for (int i = 0; i < NUM_BUTTONS; i++) {
    Serial.printf(" [GPIO %2d] %-12s -> %s\n", buttons[i].pin, buttons[i].name, buttons[i].role);
  }
  Serial.println("\nWaiting for button events...\n");
}

void loop() {
  unsigned long now = millis();

  for (int i = 0; i < NUM_BUTTONS; i++) {
    bool currentState = digitalRead(buttons[i].pin);

    // Button Pressed (Transition from HIGH to LOW)
    if (buttons[i].lastState == HIGH && currentState == LOW) {
      buttons[i].pressStartTime = now;
      buttons[i].pressCount++;
      buttons[i].longPressFired = false;
      Serial.printf("[PRESS]   Button '%s' on GPIO %d pressed! (Count: %u)\n", 
                    buttons[i].name, buttons[i].pin, buttons[i].pressCount);
      delay(20); // Basic debounce
    }

    // Button Held Down (Check for Long Press threshold)
    if (currentState == LOW && !buttons[i].longPressFired) {
      if ((now - buttons[i].pressStartTime) >= LONG_PRESS_MS) {
        buttons[i].longPressFired = true;
        Serial.printf("[HOLD]    Button '%s' held for >500ms (Long-Press action trigger!)\n", 
                      buttons[i].name);
      }
    }

    // Button Released (Transition from LOW to HIGH)
    if (buttons[i].lastState == LOW && currentState == HIGH) {
      unsigned long duration = now - buttons[i].pressStartTime;
      Serial.printf("[RELEASE] Button '%s' released after %lu ms\n", 
                    buttons[i].name, duration);
      delay(20); // Basic debounce
    }

    buttons[i].lastState = currentState;
  }
}
