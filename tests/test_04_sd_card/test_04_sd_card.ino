/*
 * ==============================================================================
 * TEST 04: INCREMENTAL TEST - OLED + ROTARY ENCODER + MICRO SD CARD
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
 * 3. Micro SD Card Module (SPI):
 *    - VCC           -> ESP32 5V (VIN)* [CRITICAL: see note below!]
 *    - GND           -> ESP32 GND
 *    - CS            -> ESP32 GPIO 10
 *    - MOSI          -> ESP32 GPIO 11
 *    - SCK           -> ESP32 GPIO 12
 *    - MISO          -> ESP32 GPIO 13
 * 
 * *IMPORTANT SD POWER NOTE:
 *  Most MicroSD breakout modules (common blue boards) have an onboard 3.3V 
 *  regulator chip. They MUST be powered from ESP32 5V (VIN) so the onboard 
 *  regulator delivers a clean 3.3V to the card.
 *  (If your board is a rare 3.3V-only board with NO regulator chip, connect to 3V3).
 * 
 * REQUIRED ARDUINO LIBRARIES:
 *  - "SdFat" by Bill Greiman
 *  - "Adafruit SSD1306" & "Adafruit GFX"
 * 
 * SERIAL MONITOR: 115200 Baud
 * ==============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <SdFat.h>
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
#define ENCODER_SW_PIN  4   // Switch (Push button)

// Micro SD Card Definitions (SPI)
#define SD_CS_PIN       10
#define SPI_MOSI_PIN    11
#define SPI_MISO_PIN    13
#define SPI_SCK_PIN     12
#define SPI_SPEED       SD_SCK_MHZ(20)

SdFat sd;
SdFile file;

// State Variables
int virtualFreqKHz = 45;    // Ultrasonic Bat Carrier (10 to 90 kHz)
int virtualVolume = 50;     // Output Volume (0 to 100 %)
enum EncoderMode { MODE_FREQ = 0, MODE_VOL = 1 };
EncoderMode currentMode = MODE_FREQ;

int encoderTicks = 0;
uint8_t lastEncState = 0;
const int TICKS_PER_DETENT = 4;

bool lastButtonReading = HIGH;
bool buttonActive = false;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 40;

// Telemetry & SD State
bool sdMounted = false;
float sdCapacityGB = 0.0f;
float sdWriteSpeedKBps = 0.0f;
int fileIndex = 0;
String lastSavedFile = "";
unsigned long saveMessageExpiry = 0;

void drawMainUI() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // --- TOP ROW: Mode Badge & SD Status ---
  display.setTextSize(1);
  if (currentMode == MODE_FREQ) {
    display.fillRect(0, 0, 38, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(2, 1);
    display.print(" FREQ ");
  } else {
    display.fillRect(0, 0, 38, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(4, 1);
    display.print(" VOL  ");
  }

  // Top right: SD Card Status & File Index
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(44, 1);
  if (sdMounted) {
    display.printf("SD:OK [#%02d]", fileIndex);
  } else {
    display.print("SD:NO CARD!");
  }

  // --- MIDDLE ROW: Value Display OR Save Feedback ---
  display.setCursor(0, 12);
  if (millis() < saveMessageExpiry) {
    // Show temporary "SAVED!" banner when a file is written
    display.fillRect(0, 11, OLED_WIDTH, 11, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(10, 13);
    display.printf("SAVED: %s", lastSavedFile.c_str());
    display.setTextColor(SSD1306_WHITE);
  } else {
    if (currentMode == MODE_FREQ) {
      display.printf("Carrier: %2d kHz", virtualFreqKHz);
    } else {
      display.printf("Volume : %3d %%", virtualVolume);
    }
  }

  // --- BOTTOM SECTION: Graphical Bar Meter ---
  int barWidth = 92;
  int barHeight = 6;
  int barX = 0;
  int barY = 24;

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

  // Bottom right action label: "TAP=LOG"
  display.setCursor(96, 23);
  display.print("TAP:LOG");

  display.display();
}

void triggerSDCardWrite() {
  if (!sdMounted) {
    Serial.println("[SD ERROR] Cannot write: SD card not mounted.");
    return;
  }

  // Auto-increment filename: LOG000.TXT, LOG001.TXT...
  char fname[24];
  snprintf(fname, sizeof(fname), "/LOG%03d.TXT", fileIndex);
  fileIndex++;

  Serial.printf("\n[SD WRITE] Creating file '%s'...\n", fname);

  if (file.open(fname, O_WRONLY | O_CREAT | O_TRUNC)) {
    file.println("==========================================");
    file.println("   ESPERDYNE BAT DETECTOR - FIELD LOG     ");
    file.println("==========================================");
    file.printf("Timestamp       : %lu ms\n", millis());
    file.printf("Carrier Freq    : %d kHz\n", virtualFreqKHz);
    file.printf("Volume Setting  : %d %%\n", virtualVolume);
    file.printf("Audio Mode      : %s\n", (currentMode == MODE_FREQ) ? "FREQUENCY" : "VOLUME");
    file.printf("Free PSRAM      : %u bytes (%.2f MB)\n", ESP.getFreePsram(), (float)ESP.getFreePsram() / (1024.0 * 1024.0));
    file.printf("Free SRAM Heap  : %u bytes\n", ESP.getFreeHeap());
    file.println("------------------------------------------");
    file.println("Simulated 5s ultrasonic dump trigger OK.");
    file.println("==========================================");
    file.sync();
    file.close();

    lastSavedFile = String(fname + 1); // e.g. "LOG001.TXT"
    saveMessageExpiry = millis() + 1500; // Display banner for 1.5s
    Serial.printf("[PASS] Successfully wrote metadata log to SD: %s\n", fname);
  } else {
    Serial.printf("[FAIL] Failed to open '%s' for writing!\n", fname);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=======================================================");
  Serial.println(" ESPERDYNE TEST 04: OLED + ROTARY ENCODER + SD CARD   ");
  Serial.println("=======================================================");

  // 1. Initialize Inputs
  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
  uint8_t a = digitalRead(ENCODER_A_PIN);
  uint8_t b = digitalRead(ENCODER_B_PIN);
  lastEncState = (a << 1) | b;

  // 2. Initialize OLED
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[FAIL] SSD1306 OLED failed to initialize!");
    while (true) delay(100);
  }

  // Stage 1: OLED Splash Screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(6, 0);
  display.print("ESPERDYNE BAT-PAM");
  display.drawLine(0, 9, OLED_WIDTH, 9, SSD1306_WHITE);
  display.setCursor(0, 12);
  display.print("Mounting SD Card...");
  display.display();

  // 3. Initialize SPI & MicroSD Card
  Serial.println("Initializing SPI Bus (MOSI=11, MISO=13, SCK=12, CS=10)...");
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS_PIN);

  if (sd.begin(SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_SPEED))) {
    sdMounted = true;
    uint32_t cardSizeMB = sd.card()->sectorCount() * 512UL / (1024UL * 1024UL);
    sdCapacityGB = (float)cardSizeMB / 1024.0f;
    Serial.printf("[PASS] SD Card Detected! Capacity: %.2f GB\n", sdCapacityGB);

    // Run quick 64KB write benchmark
    const char* benchName = "/bench.tmp";
    if (file.open(benchName, O_WRONLY | O_CREAT | O_TRUNC)) {
      uint8_t buf[512];
      memset(buf, 0xAA, sizeof(buf));
      unsigned long tStart = millis();
      for (int i = 0; i < 128; i++) file.write(buf, sizeof(buf)); // 128 * 512 = 64KB
      file.sync();
      file.close();
      unsigned long dur = millis() - tStart;
      sdWriteSpeedKBps = (64.0f * 1000.0f) / (float)dur;
      sd.remove(benchName);
      Serial.printf("[PASS] Benchmark Write Speed: %.1f KB/sec\n", sdWriteSpeedKBps);
    }

    // Display SD Success on OLED
    display.fillRect(0, 12, OLED_WIDTH, 20, SSD1306_BLACK);
    display.setCursor(0, 12);
    display.printf("SD: %.1f GB [OK]", sdCapacityGB);
    display.setCursor(0, 22);
    display.printf("Speed: %.0f KB/s", sdWriteSpeedKBps);
    display.display();
    delay(2500);

  } else {
    sdMounted = false;
    Serial.println("[FAIL] SD Card initialization failed!");
    Serial.println("-> Check: VCC to 5V (VIN), GND, MOSI=11, MISO=13, SCK=12, CS=10.");

    display.fillRect(0, 12, OLED_WIDTH, 20, SSD1306_BLACK);
    display.setCursor(0, 12);
    display.print("SD Mount FAILED!");
    display.setCursor(0, 22);
    display.print("Check 5V & Wires");
    display.display();
    delay(3000);
  }

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
        Serial.printf("[ENCODER] Carrier: %2d kHz\n", virtualFreqKHz);
      } else {
        virtualVolume += step * 5;
        if (virtualVolume < 0) virtualVolume = 0;
        if (virtualVolume > 100) virtualVolume = 100;
        Serial.printf("[ENCODER] Volume: %3d %%\n", virtualVolume);
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
      // Write log file to SD card and toggle mode!
      triggerSDCardWrite();
      currentMode = (currentMode == MODE_FREQ) ? MODE_VOL : MODE_FREQ;
      needsRedraw = true;
    } else if (reading == HIGH && buttonActive) {
      buttonActive = false;
    }
  }

  // Clear "SAVED" message after 1.5 seconds
  if (saveMessageExpiry > 0 && millis() > saveMessageExpiry) {
    saveMessageExpiry = 0;
    needsRedraw = true;
  }

  if (needsRedraw) {
    drawMainUI();
  }
}
