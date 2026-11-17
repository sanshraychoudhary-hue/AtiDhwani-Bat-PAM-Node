/*
 * ESPERDYNE PHASE 1 TEST FIRMWARE
 * --------------------------------
 * HARDWARE PINOUT / WIRING GUIDE:
 * 
 * 1. OLED Display (I2C)
 *    - VCC -> 3.3V
 *    - GND -> GND
 *    - SDA -> GPIO 8
 *    - SCL -> GPIO 9
 * 
 * 2. Rotary Encoder
 *    - GND -> GND
 *    - VCC -> 3.3V (if it has a VCC pin, otherwise ignore)
 *    - CLK (A) -> GPIO 6
 *    - DT (B)  -> GPIO 5
 *    - SW (Button) -> GPIO 4
 * 
 * 3. Tactile Buttons (Wire one side to GND, other side to GPIO)
 *    - Record Button   -> GPIO 7
 *    - CH2 Edit Button -> GPIO 1
 *    - OLED Toggle     -> GPIO 3
 *    - Mix Toggle      -> GPIO 2
 * 
 * 4. Micro SD Card Module (SPI)
 *    - VCC -> 5V (If using the blue module with onboard LDO) or 3.3V
 *    - GND -> GND
 *    - CS  -> GPIO 10
 *    - MOSI -> GPIO 11
 *    - SCK  -> GPIO 12
 *    - MISO -> GPIO 13
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <SdFat.h>
#include <Fonts/FreeSerif9pt7b.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_RESET -1
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

#define RECORD_BUTTON_PIN 7
#define CH2_HET_BUTTON_PIN 1
#define OLED_TOGGLE_PIN 3
#define MIX_TOGGLE_PIN 2
#define ENCODER_A_PIN 6
#define ENCODER_B_PIN 5
#define ENCODER_SW_PIN 4

#define SD_CS_PIN 10
#define SPI_MOSI 11
#define SPI_MISO 13
#define SPI_SCK 12
#define SPI_SPEED SD_SCK_MHZ(25)

float carrierFreq1 = 45000.0f;
float carrierFreq2 = 45000.0f;
float outGain = 0.5f;
bool mixingMode = true;
unsigned long lastMixButtonTime = 0;

int fileIndex = 0;
int encoderTicks = 0;
uint8_t lastEncState = 0;
const int TICKS_PER_DETENT = 4;

enum EditMode { EDIT_FREQ = 0, EDIT_VOL = 1 };
EditMode editMode = EDIT_FREQ;

const float FREQ_MIN = 10000.0f, FREQ_MAX = 85000.0f, FREQ_STEP = 5000.0f;
const float VOL_STEP = 0.05f;

bool saving = false;
bool oledOn = true;
unsigned long lastOLEDButtonTime = 0;
bool ch2AdjustMode = false;

SdFat sd;
SdFile file;

void drawUI() {
  if (!oledOn) return;
  display.clearDisplay();
  
  display.setFont();
  display.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds("ESPERDYNE TEST", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((OLED_WIDTH - w) / 2, 25);
  display.print("ESPERDYNE TEST");

  display.setCursor(0, 0); display.print("kHz");
  display.setCursor(28, 0); display.print("Mode");
  display.setCursor(60, 0); display.print("%");
  display.setCursor(110, 0); display.print("kHz");
  display.setCursor(85, 0); display.print("#");

  display.setFont(&FreeSerif9pt7b);
  display.setCursor(0, 21); display.print(int(carrierFreq1 / 1000.0f));
  display.setCursor(110, 21); display.print(int(carrierFreq2 / 1000.0f));

  display.setCursor(31, 21); display.print(editMode == EDIT_FREQ ? "F" : "V");
  display.setCursor(53, 21); display.print((int)(outGain * 100));
  display.setCursor(82, 21); display.print(fileIndex);

  display.setFont();
  display.setTextSize(1);
  display.setCursor(0, OLED_HEIGHT - 8);
  display.print(mixingMode ? "MX" : "ST");

  display.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(RECORD_BUTTON_PIN, INPUT_PULLUP);
  pinMode(CH2_HET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(OLED_TOGGLE_PIN, INPUT_PULLUP);
  pinMode(MIX_TOGGLE_PIN, INPUT_PULLUP);
  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);

  Wire.begin(); // Uses default ESP32-S3 SDA/SCL pins
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.setRotation(2); // Matches original firmware
  display.setTextColor(WHITE);
  display.clearDisplay();
  display.setCursor(0,0);
  display.print("Booting Test...");
  display.display();
  delay(1000);

  // Initialize SPI for SD Card using defined pins
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS_PIN);
  if (!sd.begin(SD_CS_PIN, SPI_SPEED)) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("SD INIT FAIL");
    display.display();
    delay(2000);
  } else {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("SD INIT SUCCESS!");
    display.display();
    delay(1000);
  }

  drawUI();
  uint8_t a = digitalRead(ENCODER_A_PIN);
  uint8_t b = digitalRead(ENCODER_B_PIN);
  lastEncState = (a << 1) | b;
}

void loop() {
  // 1. OLED Toggle Button Test
  if (digitalRead(OLED_TOGGLE_PIN) == LOW && millis() - lastOLEDButtonTime > 500) {
    oledOn = !oledOn;
    lastOLEDButtonTime = millis();
    if (!oledOn) {
      display.clearDisplay();
      display.display();
      display.ssd1306_command(SSD1306_DISPLAYOFF);
    } else {
      display.ssd1306_command(SSD1306_DISPLAYON);
      drawUI();
    }
  }

  // 2. Mix Mode Toggle Test
  if (digitalRead(MIX_TOGGLE_PIN) == LOW && millis() - lastMixButtonTime > 500) {
    mixingMode = !mixingMode;
    lastMixButtonTime = millis();
    drawUI();
  }

  // 3. Channel 2 Edit Mode (Hold to adjust right channel)
  ch2AdjustMode = (digitalRead(CH2_HET_BUTTON_PIN) == LOW);

  // 4. Record Button Test (Writes a dummy TXT file to SD)
  if (digitalRead(RECORD_BUTTON_PIN) == LOW && !saving) {
    saving = true;
    display.clearDisplay();
    display.setFont(&FreeSerif9pt7b);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds("Saving Test...", 0, 0, &x1, &y1, &w, &h);
    display.setCursor((OLED_WIDTH - w) / 2, 16);
    display.print("Saving Test...");
    display.display();

    // Create a dummy file to test SD writing
    char fname[32];
    snprintf(fname, sizeof(fname), "/TEST%03d.TXT", fileIndex++);
    if (file.open(fname, O_WRONLY | O_CREAT | O_TRUNC)) {
      file.println("This is a test file to verify SD card writing on Esperdyne hardware.");
      file.close();
      Serial.println("Saved file: " + String(fname));
    } else {
      Serial.println("Failed to open file for writing!");
    }
    
    delay(500); // Simulate saving time
    saving = false;
    drawUI();
  }

  // 5. Rotary Encoder Logic Test
  uint8_t a = digitalRead(ENCODER_A_PIN), b = digitalRead(ENCODER_B_PIN);
  uint8_t state = (a << 1) | b;
  static const int8_t enc_table[] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
  int8_t movement = enc_table[(lastEncState << 2) | state];
  lastEncState = state;
  if (movement != 0) {
    encoderTicks += movement;
    int detents = -1 * encoderTicks / TICKS_PER_DETENT;
    encoderTicks %= TICKS_PER_DETENT;
    if (detents != 0) {
      if (editMode == EDIT_FREQ) {
        if (ch2AdjustMode) {
          carrierFreq2 = constrain(carrierFreq2 + detents * FREQ_STEP, FREQ_MIN, FREQ_MAX);
        } else {
          carrierFreq1 = constrain(carrierFreq1 + detents * FREQ_STEP, FREQ_MIN, FREQ_MAX);
        }
      } else {
        outGain = constrain(outGain + detents * VOL_STEP, 0.0f, 1.0f);
      }
      drawUI();
    }
  }

  // 6. Encoder Switch Button Test (Toggle Freq / Vol mode)
  if (digitalRead(ENCODER_SW_PIN) == LOW) {
    delay(200);
    editMode = (editMode == EDIT_FREQ) ? EDIT_VOL : EDIT_FREQ;
    drawUI();
  }
}
