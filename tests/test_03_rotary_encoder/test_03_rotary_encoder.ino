/*
 * ==============================================================================
 * TEST 03: INCREMENTAL TEST - OLED + ROTARY ENCODER + PSRAM TELEMETRY
 * ==============================================================================
 * HARDWARE PINOUT / WIRING GUIDE:
 * 
 * 1. 0.91" 128x32 OLED Display (I2C):
 *    - VCC           -> ESP32 3V3
 *    - GND           -> ESP32 GND
 *    - SDA           -> ESP32 GPIO 8
 *    - SCL / SCK     -> ESP32 GPIO 9
 * 
 * 2. EC11 Rotary Encoder:
 *    - GND           -> ESP32 GND
 *    - VCC / '+'     -> ESP32 3V3 (Only if breakout board has a '+' pin; ignore if bare)
 *    - CLK (Pin A)   -> ESP32 GPIO 6
 *    - DT  (Pin B)   -> ESP32 GPIO 5
 *    - SW  (Switch)  -> ESP32 GPIO 4
 * 
 * Note: If using a bare 5-pin metal rotary encoder (without breakout PCB):
 *    - 3 pins side: Middle pin -> GND, Left/Right outer pins -> GPIO 6 & GPIO 5.
 *    - 2 pins side (Switch): One pin -> GND, Other pin -> GPIO 4.
 * Internal pull-up resistors (INPUT_PULLUP) are enabled on all inputs.
 * 
 * SERIAL MONITOR: 115200 Baud
 * ==============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "esp_heap_caps.h"

// OLED Definitions
#define OLED_WIDTH    128
#define OLED_HEIGHT   32
#define OLED_RESET    -1
#define OLED_ADDR     0x3C
#define PIN_I2C_SDA   8
#define PIN_I2C_SCL   9

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// Rotary Encoder Definitions
#define ENCODER_A_PIN   6   // CLK
#define ENCODER_B_PIN   5   // DT
#define ENCODER_SW_PIN  4   // Push Button Switch

// State Variables
int virtualFreqKHz = 45;    // Ultrasonic Bat Carrier (10 kHz to 90 kHz)
int virtualVolume = 50;     // Output Volume (0 to 100 %)
enum EncoderMode { MODE_FREQ = 0, MODE_VOL = 1 };
EncoderMode currentMode = MODE_FREQ;

// Quadrature Decoding
int encoderTicks = 0;
uint8_t lastEncState = 0;
const int TICKS_PER_DETENT = 4; // Standard for EC11 detents

// Switch Timing & State
bool lastButtonReading = HIGH;
bool buttonActive = false;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 40;

// Hardware Telemetry Cache
size_t totalPSRAM = 0;
size_t freePSRAM = 0;
uint32_t freeHeap = 0;
bool psramOk = false;
bool ringBufferAllocOk = false;

void drawMainUI() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // --- TOP ROW: Mode Badge & Live System Stats ---
  display.setTextSize(1);
  if (currentMode == MODE_FREQ) {
    // Inverted highlight badge for FREQ mode
    display.fillRect(0, 0, 42, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(2, 1);
    display.print(" FREQ ");
  } else {
    // Inverted highlight badge for VOL mode
    display.fillRect(0, 0, 42, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(5, 1);
    display.print(" VOL  ");
  }

  // Live PSRAM Telemetry at top right
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(48, 1);
  display.printf("PSR:%.1fMB", (float)freePSRAM / (1024.0 * 1024.0));

  // --- MIDDLE ROW: Value Display ---
  display.setCursor(0, 12);
  if (currentMode == MODE_FREQ) {
    display.printf("Carrier: %2d kHz", virtualFreqKHz);
  } else {
    display.printf("Volume : %3d %%", virtualVolume);
  }

  // --- BOTTOM SECTION: Graphical Bar Meter & Button Indicator ---
  int barWidth = 96;
  int barHeight = 6;
  int barX = 0;
  int barY = 24;

  // Draw meter bounding box
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);

  int fillWidth = 0;
  if (currentMode == MODE_FREQ) {
    fillWidth = map(constrain(virtualFreqKHz, 10, 90), 10, 90, 0, barWidth - 2);
  } else {
    fillWidth = map(constrain(virtualVolume, 0, 100), 0, 100, 0, barWidth - 2);
  }
  if (fillWidth > 0) {
    display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, SSD1306_WHITE);
  }

  // Right side of bottom row: Switch press indicator
  display.setCursor(102, 23);
  if (buttonActive) {
    display.fillRect(100, 22, 28, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.print("SW!");
  } else {
    display.print("Enc");
  }

  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=======================================================");
  Serial.println("  ESPERDYNE TEST 03: INCREMENTAL OLED + ENCODER + PSRAM ");
  Serial.println("=======================================================");

  // 1. Initialize Inputs with internal pull-ups
  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);

  uint8_t a = digitalRead(ENCODER_A_PIN);
  uint8_t b = digitalRead(ENCODER_B_PIN);
  lastEncState = (a << 1) | b;

  // 2. Validate PSRAM & Telemetry from Test 01
  if (psramInit()) {
    psramOk = true;
    totalPSRAM = ESP.getPsramSize();
    freePSRAM = ESP.getFreePsram();

    // Verify 3.84 MB ring buffer allocatable
    size_t ringBytes = 192000 * 2 * 5 * sizeof(int16_t);
    int16_t* testBuf = (int16_t*)heap_caps_malloc(ringBytes, MALLOC_CAP_SPIRAM);
    if (testBuf != nullptr) {
      ringBufferAllocOk = true;
      heap_caps_free(testBuf);
    }
  }
  freeHeap = ESP.getFreeHeap();

  // 3. Initialize OLED
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[FAIL] SSD1306 allocation failed! Check SDA/SCL wires.");
    while (true) delay(100);
  }
  Serial.println("[PASS] OLED initialized successfully!");

  // 4. OLED Stage 1: Hardware & PSRAM Splash Screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(8, 0);
  display.print("ESPERDYNE BAT-PAM");

  display.drawLine(0, 9, OLED_WIDTH, 9, SSD1306_WHITE);

  display.setCursor(0, 12);
  display.printf("PSRAM : %.2f MB %s", (float)totalPSRAM / (1024.0 * 1024.0), psramOk ? "OK" : "ERR");

  display.setCursor(0, 22);
  display.printf("RingBuf: 3.84MB %s", ringBufferAllocOk ? "PASS" : "FAIL");

  display.display();

  Serial.println("\n[OLED Telemetry Displayed]");
  Serial.printf(" - Total PSRAM  : %.2f MB\n", (float)totalPSRAM / (1024.0 * 1024.0));
  Serial.printf(" - Free PSRAM   : %.2f MB\n", (float)freePSRAM / (1024.0 * 1024.0));
  Serial.printf(" - Free Heap    : %u KB\n", freeHeap / 1024);
  Serial.printf(" - 3.84MB Buffer: %s\n\n", ringBufferAllocOk ? "ALLOCATED & VERIFIED" : "FAILED");

  delay(3000); // Display telemetry on screen for 3 seconds

  // Switch to main interactive UI
  drawMainUI();
}

void loop() {
  bool needsRedraw = false;

  // 1. Read Rotary Encoder Quadrature Transition
  uint8_t a = digitalRead(ENCODER_A_PIN);
  uint8_t b = digitalRead(ENCODER_B_PIN);
  uint8_t currentState = (a << 1) | b;

  if (currentState != lastEncState) {
    // Quadrature state transition table
    // CW : 00 -> 01 -> 11 -> 10
    // CCW: 00 -> 10 -> 11 -> 01
    if ((lastEncState == 0b00 && currentState == 0b01) ||
        (lastEncState == 0b01 && currentState == 0b11) ||
        (lastEncState == 0b11 && currentState == 0b10) ||
        (lastEncState == 0b10 && currentState == 0b00)) {
      encoderTicks++;
    } else if ((lastEncState == 0b00 && currentState == 0b10) ||
               (lastEncState == 0b10 && currentState == 0b11) ||
               (lastEncState == 0b11 && currentState == 0b01) ||
               (lastEncState == 0b01 && currentState == 0b00)) {
      encoderTicks--;
    }

    lastEncState = currentState;

    if (abs(encoderTicks) >= TICKS_PER_DETENT) {
      int step = encoderTicks / TICKS_PER_DETENT;
      encoderTicks %= TICKS_PER_DETENT;

      if (currentMode == MODE_FREQ) {
        virtualFreqKHz += step;
        if (virtualFreqKHz < 10) virtualFreqKHz = 10;
        if (virtualFreqKHz > 90) virtualFreqKHz = 90;
        Serial.printf("[ROTARY CW/CCW] Carrier Frequency: %2d kHz\n", virtualFreqKHz);
      } else {
        virtualVolume += step * 5;
        if (virtualVolume < 0) virtualVolume = 0;
        if (virtualVolume > 100) virtualVolume = 100;
        Serial.printf("[ROTARY CW/CCW] Volume: %3d %%\n", virtualVolume);
      }

      needsRedraw = true;
    }
  }

  // 2. Read Rotary Encoder Push-Button (SW)
  int reading = digitalRead(ENCODER_SW_PIN);
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
    lastButtonReading = reading;
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading == LOW && !buttonActive) {
      buttonActive = true;
      // Toggle between Frequency and Volume Mode
      currentMode = (currentMode == MODE_FREQ) ? MODE_VOL : MODE_FREQ;
      Serial.printf("\n[SWITCH CLICK] Mode Switched to: %s\n", 
                    (currentMode == MODE_FREQ) ? "FREQUENCY TUNING" : "VOLUME CONTROL");
      needsRedraw = true;
    } else if (reading == HIGH && buttonActive) {
      buttonActive = false;
      needsRedraw = true;
    }
  }

  // 3. Periodic PSRAM telemetry update & screen redraw
  static unsigned long lastTelemetryUpdate = 0;
  if (millis() - lastTelemetryUpdate > 1000) {
    lastTelemetryUpdate = millis();
    freePSRAM = ESP.getFreePsram();
    freeHeap = ESP.getFreeHeap();
  }

  if (needsRedraw) {
    drawMainUI();
  }
}
