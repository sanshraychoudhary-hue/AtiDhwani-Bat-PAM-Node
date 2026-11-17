/*
 * ==============================================================================
 * ESPERDYNE BAT DETECTOR - LAYER 1 HARDWARE VERIFICATION & INTEGRATION TEST
 * (FAIL-SAFE VERSION: Boots cleanly with or without peripherals connected!)
 * ==============================================================================
 * Tests all components soldered on the primary Zero-PCB:
 * 1. ESP32-S3 (8MB Octal PSRAM + Dual Core FreeRTOS)
 * 2. 0.91" 128x32 I2C OLED (SDA=8, SCL=9)
 * 3. MicroSD Card Module (SPI: CS=10, MOSI=11, SCK=12, MISO=13)
 * 4. PCM5102A 32-bit I2S DAC (BCK=15, DIN=16, LCK=17)
 * 5. EC11 Rotary Encoder + Push Switch (CLK=6, DT=5, SW=4)
 * 
 * SERIAL MONITOR: 115200 Baud (Works on BOTH USB and UART ports!)
 * ==============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"
#include "esp_heap_caps.h"
#include <math.h>

// ------------------------------------------------------------------------------
// SD CARD COMPATIBILITY LAYER
// ------------------------------------------------------------------------------
#if __has_include(<SdFat.h>)
  #include <SdFat.h>
  #define USE_SDFAT 1
  SdFat sd;
  SdFile sdTestFile;
#else
  #include <SD.h>
  #include <FS.h>
  #define USE_SDFAT 0
  File sdTestFile;
#endif

// ------------------------------------------------------------------------------
// PIN DEFINITIONS
// ------------------------------------------------------------------------------
#define OLED_WIDTH        128
#define OLED_HEIGHT       32
#define OLED_RESET        -1
#define OLED_ADDR         0x3C
#define PIN_I2C_SDA       8
#define PIN_I2C_SCL       9

#define PIN_ENC_CLK       6   // S1
#define PIN_ENC_DT        5   // S2
#define PIN_ENC_SW        4   // KEY / Switch

#define PIN_SD_CS         10
#define PIN_SD_MOSI       11  // MO
#define PIN_SD_SCK        12  // SCK
#define PIN_SD_MISO       13  // MI

#define I2S_OUT_PORT      I2S_NUM_1
#define PIN_DAC_BCK       15
#define PIN_DAC_DIN       16  // DO
#define PIN_DAC_LCK       17  // WS

#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_BUF_SAMPLES 128

// ------------------------------------------------------------------------------
// GLOBAL OBJECTS & STATE
// ------------------------------------------------------------------------------
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

bool oledFound = false;
bool psramDetected = false;
size_t totalPSRAM = 0;
size_t freePSRAM = 0;
bool sdMounted = false;
uint32_t sdCapacityMB = 0;
char sdStatusMsg[24] = "Not Connected";

enum ControlMode { MODE_FREQ = 0, MODE_VOL = 1 };
ControlMode currentMode = MODE_FREQ;

volatile float g_toneFreq = 440.0f;
volatile int g_volume = 70;
volatile bool g_triggerChime = false;

int encoderTicks = 0;
uint8_t lastEncState = 0;
const int TICKS_PER_DETENT = 4;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
bool buttonPressedFlag = false;

int16_t dacAudioBuffer[AUDIO_BUF_SAMPLES * 2];

// ------------------------------------------------------------------------------
// DUAL SERIAL PRINT HELPER (Outputs to both USB-CDC and UART0)
// ------------------------------------------------------------------------------
void logMsg(const char* msg) {
  Serial.print(msg);
  Serial0.print(msg);
}

void logMsgf(const char* format, ...) {
  char buf[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  Serial.print(buf);
  Serial0.print(buf);
}

// ------------------------------------------------------------------------------
// PCM5102A I2S DRIVER
// ------------------------------------------------------------------------------
void setupI2SDAC() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = AUDIO_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = AUDIO_BUF_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };

  i2s_pin_config_t pin_config = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = PIN_DAC_BCK,
    .ws_io_num = PIN_DAC_LCK,
    .data_out_num = PIN_DAC_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err = i2s_driver_install(I2S_OUT_PORT, &i2s_config, 0, NULL);
  if (err == ESP_OK) {
    i2s_set_pin(I2S_OUT_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_OUT_PORT);
    logMsg("[PASS] PCM5102A I2S DAC Driver initialized!\n");
  } else {
    logMsgf("[FAIL] I2S DAC init error: %d\n", err);
  }
}

// ------------------------------------------------------------------------------
// BACKGROUND AUDIO GENERATOR TASK (Core 1 FreeRTOS)
// ------------------------------------------------------------------------------
void audioTask(void* parameter) {
  float phase = 0.0f;
  float chimePhase = 0.0f;
  int chimeSamplesRemaining = 0;

  while (true) {
    if (g_triggerChime) {
      g_triggerChime = false;
      chimeSamplesRemaining = AUDIO_SAMPLE_RATE / 5;
      chimePhase = 0.0f;
    }

    float currentFreq = g_toneFreq;
    float currentVolFrac = (float)g_volume / 100.0f;
    float phaseInc = (2.0f * M_PI * currentFreq) / (float)AUDIO_SAMPLE_RATE;
    float chimePhaseInc = (2.0f * M_PI * 1800.0f) / (float)AUDIO_SAMPLE_RATE;

    for (int i = 0; i < AUDIO_BUF_SAMPLES; i++) {
      int16_t sample = 0;

      if (chimeSamplesRemaining > 0) {
        sample = (int16_t)(sin(chimePhase) * 26000.0f * currentVolFrac);
        chimePhase += chimePhaseInc;
        chimeSamplesRemaining--;
      } else {
        sample = (int16_t)(sin(phase) * 22000.0f * currentVolFrac);
        phase += phaseInc;
        if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
      }

      dacAudioBuffer[i * 2]     = sample;
      dacAudioBuffer[i * 2 + 1] = sample;
    }

    size_t bytes_written = 0;
    i2s_write(I2S_OUT_PORT, dacAudioBuffer, sizeof(dacAudioBuffer), &bytes_written, portMAX_DELAY);
  }
}

// ------------------------------------------------------------------------------
// MICRO SD CARD TEST (NON-BLOCKING)
// ------------------------------------------------------------------------------
bool testSDCard() {
  logMsg("[SD] Testing MicroSD card... ");
  sdMounted = false;

#if USE_SDFAT
  if (sd.begin(SdSpiConfig(PIN_SD_CS, DEDICATED_SPI, SD_SCK_MHZ(10)))) {
    sdMounted = true;
    sdCapacityMB = sd.card()->sectorCount() * 512UL / (1024UL * 1024UL);
    if (sdTestFile.open("/layer1_test.txt", O_WRONLY | O_CREAT | O_TRUNC)) {
      sdTestFile.println("Esperdyne Layer 1 Zero-PCB Test OK!");
      sdTestFile.close();
      snprintf(sdStatusMsg, sizeof(sdStatusMsg), "OK %uMB", sdCapacityMB);
      logMsgf("[PASS] %s\n", sdStatusMsg);
      return true;
    }
  }
#else
  if (SD.begin(PIN_SD_CS, SPI, 10000000)) {
    sdMounted = true;
    sdCapacityMB = SD.cardSize() / (1024 * 1024);
    File f = SD.open("/layer1_test.txt", FILE_WRITE);
    if (f) {
      f.println("Esperdyne Layer 1 Zero-PCB Test OK!");
      f.close();
      snprintf(sdStatusMsg, sizeof(sdStatusMsg), "OK %uMB", sdCapacityMB);
      logMsgf("[PASS] %s\n", sdStatusMsg);
      return true;
    }
  }
#endif

  snprintf(sdStatusMsg, sizeof(sdStatusMsg), "NO CARD/FAIL");
  logMsg("[WARN] No SD card detected or card read error.\n");
  return false;
}

// ------------------------------------------------------------------------------
// OLED DISPLAY RENDER
// ------------------------------------------------------------------------------
void renderOLED() {
  if (!oledFound) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("PSR:%dM ", (int)(totalPSRAM / (1024 * 1024)));
  display.printf("SD:%s", sdStatusMsg);

  display.setCursor(0, 11);
  if (currentMode == MODE_FREQ) {
    display.printf(">TONE: %4.0fHz  V:%d%%", g_toneFreq, g_volume);
  } else {
    display.printf(" TONE: %4.0fHz >V:%d%%", g_toneFreq, g_volume);
  }

  int barWidth = 92;
  int barHeight = 7;
  display.drawRect(0, 23, barWidth, barHeight, SSD1306_WHITE);

  int fillWidth = 0;
  if (currentMode == MODE_FREQ) {
    fillWidth = map(constrain((int)g_toneFreq, 100, 3000), 100, 3000, 0, barWidth - 2);
  } else {
    fillWidth = map(constrain(g_volume, 0, 100), 0, 100, 0, barWidth - 2);
  }
  if (fillWidth > 0) {
    display.fillRect(1, 24, fillWidth, barHeight - 2, SSD1306_WHITE);
  }

  display.setCursor(98, 23);
  if (buttonPressedFlag) {
    display.fillRect(96, 22, 32, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.print("CLICK");
  } else {
    display.print(currentMode == MODE_FREQ ? "[FRQ]" : "[VOL]");
  }

  display.display();
}

// ------------------------------------------------------------------------------
// SETUP
// ------------------------------------------------------------------------------
void setup() {
  // Start BOTH UART0 and Native USB Serial
  Serial.begin(115200);
  Serial0.begin(115200);
  delay(1000);

  logMsg("\n=======================================================\n");
  logMsg("  ESPERDYNE BAT DETECTOR - LAYER 1 HARDWARE TEST        \n");
  logMsg("=======================================================\n");

  // 1. ESP32-S3 PSRAM Check
  if (psramInit()) {
    psramDetected = true;
    totalPSRAM = ESP.getPsramSize();
    freePSRAM = ESP.getFreePsram();
    logMsgf("[PASS] Octal PSRAM Detected: %u Bytes (%.2f MB)\n", 
            totalPSRAM, (float)totalPSRAM / (1024.0 * 1024.0));
  } else {
    logMsg("[WARN] PSRAM not detected! Make sure Tools -> PSRAM is set to 'OPI PSRAM'.\n");
  }

  // 2. Rotary Encoder Pins
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  uint8_t a = digitalRead(PIN_ENC_CLK);
  uint8_t b = digitalRead(PIN_ENC_DT);
  lastEncState = (a << 1) | b;
  logMsg("[PASS] Rotary Encoder configured (CLK=6, DT=5, SW=4).\n");

  // 3. I2C OLED with BUS TIMEOUT PROBE
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setTimeOut(30); // 30ms timeout prevents hanging if no OLED is attached!

  // Probe I2C address 0x3C
  Wire.beginTransmission(OLED_ADDR);
  byte i2cErr = Wire.endTransmission();

  if (i2cErr == 0) {
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
      oledFound = true;
      logMsg("[PASS] 0.91\" I2C OLED initialized at 0x3C.\n");
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(10, 4);
      display.print("ESPERDYNE LAYER 1");
      display.setCursor(14, 18);
      display.print("TESTING MODULES...");
      display.display();
    }
  } else {
    logMsg("[INFO] OLED not found on I2C (Address 0x3C). Skipping display...\n");
  }

  // 4. MicroSD SPI Bus
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  testSDCard();

  // 5. PCM5102A I2S DAC & Background Audio Task
  setupI2SDAC();
  xTaskCreatePinnedToCore(
    audioTask,
    "AudioTask",
    4096,
    NULL,
    2,
    NULL,
    1
  );
  logMsg("[PASS] Audio Background Task running on Core 1.\n");

  logMsg("=======================================================\n");
  logMsg("READY! Connect board into Zero-PCB socket to test all.\n");
  logMsg("=======================================================\n\n");

  if (oledFound) {
    delay(800);
    renderOLED();
  }
}

// ------------------------------------------------------------------------------
// MAIN LOOP
// ------------------------------------------------------------------------------
void loop() {
  bool needsRedraw = false;

  // 1. Rotary Encoder
  uint8_t a = digitalRead(PIN_ENC_CLK);
  uint8_t b = digitalRead(PIN_ENC_DT);
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
        g_toneFreq += step * 50.0f;
        if (g_toneFreq < 150.0f)  g_toneFreq = 150.0f;
        if (g_toneFreq > 3000.0f) g_toneFreq = 3000.0f;
        logMsgf("[ROTARY] Audio Tone: %.0f Hz\n", g_toneFreq);
      } else {
        g_volume += step * 5;
        if (g_volume < 0)   g_volume = 0;
        if (g_volume > 100) g_volume = 100;
        logMsgf("[ROTARY] Audio Volume: %d %%\n", g_volume);
      }
      needsRedraw = true;
    }
  }

  // 2. Rotary Encoder Switch
  int swReading = digitalRead(PIN_ENC_SW);
  if (swReading != lastButtonState) {
    lastDebounceTime = millis();
    lastButtonState = swReading;
  }

  if ((millis() - lastDebounceTime) > 40) {
    if (swReading == LOW && !buttonPressedFlag) {
      buttonPressedFlag = true;
      g_triggerChime = true;

      currentMode = (currentMode == MODE_FREQ) ? MODE_VOL : MODE_FREQ;
      testSDCard();

      logMsgf("[CLICK] Mode: %s | SD Status: %s\n", 
              currentMode == MODE_FREQ ? "FREQ TUNE" : "VOLUME", sdStatusMsg);
      needsRedraw = true;
    } else if (swReading == HIGH && buttonPressedFlag) {
      buttonPressedFlag = false;
      needsRedraw = true;
    }
  }

  // 3. Periodic UI Redraw & Serial Heartbeat
  static unsigned long lastRedraw = 0;
  if (needsRedraw || (millis() - lastRedraw > 80)) {
    lastRedraw = millis();
    renderOLED();
  }

  // Heartbeat every 5 seconds so you know it's alive even without screen
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 5000) {
    lastHeartbeat = millis();
    logMsgf("[ALIVE] ESP32-S3 Running | Free PSRAM: %.2f MB | Free Heap: %d KB\n",
            (float)ESP.getFreePsram() / (1024.0 * 1024.0), ESP.getFreeHeap() / 1024);
  }
}
