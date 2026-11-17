/*
 * ==============================================================================
 * ESPERDYNE HELPER: 2ND ESP32 ULTRASONIC & BAT CHIRP SIGNAL GENERATOR
 * ==============================================================================
 * Flash this onto ANY spare ESP32 (Classic ESP32, ESP32-S3, ESP32-C3, etc.)
 * to inject crystal-locked 30 kHz – 80 kHz test signals directly into the
 * WM8782 ADC RCA port — no microphone or sound card required!
 * 
 * WIRING (Just 2 Jumper Wires):
 *   2nd ESP32 [GPIO 4]  --->  Esperdyne WM8782 ADC [RCA Center Pin]
 *   2nd ESP32 [GND]     --->  Esperdyne WM8782 ADC [RCA Outer Ring / GND]
 * 
 * SAFETY NOTE:
 *   Uses an ~8% duty cycle pulse (~0.5V AC fundamental) so the ADC input
 *   cannot clip or overload (set Esperdyne GNx to 1x or 2x during test!).
 * 
 * TEST SEQUENCE (Cycles automatically every 4 seconds):
 *   1. 30 kHz Continuous Tone
 *   2. 42 kHz Continuous Tone
 *   3. 65 kHz Continuous Tone
 *   4. 80 kHz Continuous Tone
 *   5. 45 kHz Bat Chirp Pulses (10 chirps/sec -> tests HET + FD + AUTO Trigger!)
 * 
 * SERIAL MONITOR: 115200 Baud
 * ==============================================================================
 */

#include <Arduino.h>

#define SIG_OUT_PIN   4
#define PWM_RES_BITS  8
#define SAFE_DUTY     20   // ~8% of 255 -> clean ~0.5V AC line-level fundamental

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  void setToneHz(uint32_t freqHz, bool on) {
    if (freqHz > 0) ledcChangeFrequency(SIG_OUT_PIN, freqHz, PWM_RES_BITS);
    ledcWrite(SIG_OUT_PIN, on ? SAFE_DUTY : 0);
  }
#else
  void setToneHz(uint32_t freqHz, bool on) {
    if (freqHz > 0) ledcSetup(0, freqHz, PWM_RES_BITS);
    ledcWrite(0, on ? SAFE_DUTY : 0);
  }
#endif

void setup() {
  Serial.begin(115200);
  delay(800);

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(SIG_OUT_PIN, 30000, PWM_RES_BITS);
#else
  ledcSetup(0, 30000, PWM_RES_BITS);
  ledcAttachPin(SIG_OUT_PIN, 0);
#endif
  setToneHz(30000, false);

  Serial.println("\n=======================================================");
  Serial.println("  2ND ESP32 ULTRASONIC & BAT CHIRP TEST GENERATOR      ");
  Serial.println("=======================================================");
  Serial.println("Wire: GPIO 4 -> ADC RCA Center | GND -> ADC GND");
  Serial.println("Tip : Set Esperdyne GNx to 1x or 2x & open SPECTRUM!\n");
}

void runContinuousTone(uint32_t freqHz, uint32_t durationMs) {
  Serial.printf("[OUT] Generating %u kHz Continuous Tone...\n", freqHz / 1000);
  setToneHz(freqHz, true);
  delay(durationMs);
}

void runBatChirps(uint32_t freqHz, int chirpCount) {
  Serial.printf("[OUT] Generating %u kHz Simulated Bat Chirp Pulses...\n", freqHz / 1000);
  for (int i = 0; i < chirpCount; i++) {
    setToneHz(freqHz, true);
    delay(6);   // 6 ms ultrasonic bat chirp
    setToneHz(freqHz, false);
    delay(94);  // 10 chirps per second
  }
}

void loop() {
  runContinuousTone(30000, 4000); // Stage 1: 30 kHz
  runContinuousTone(42000, 4000); // Stage 2: 42 kHz
  runContinuousTone(65000, 4000); // Stage 3: 65 kHz
  runContinuousTone(80000, 4000); // Stage 4: 80 kHz
  runBatChirps(45000, 40);        // Stage 5: 4 seconds of 45 kHz bat pulses!
}

