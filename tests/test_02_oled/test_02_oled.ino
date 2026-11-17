/*
 * ==============================================================================
 * TEST 02: 0.91" 128x32 I2C OLED DISPLAY (SSD1306) TEST
 * ==============================================================================
 * HARDWARE PINOUT / WIRING GUIDE:
 * 
 * ESP32-S3 DevKit Pin   <---->   0.91" OLED Module Pin
 * -----------------------------------------------------
 *  3V3 (3.3V)           <---->   VCC
 *  GND                  <---->   GND
 *  GPIO 8               <---->   SDA  (Data)
 *  GPIO 9               <---->   SCL / SCK (Clock)
 * 
 * REQUIRED ARDUINO LIBRARIES (Install via Arduino Library Manager):
 * 1. "Adafruit SSD1306" by Adafruit
 * 2. "Adafruit GFX Library" by Adafruit
 * 
 * SERIAL MONITOR: 115200 Baud
 * ==============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_RESET -1
#define OLED_ADDR 0x3C

#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

void scanI2CBus() {
  Serial.println("\n[I2C] Scanning I2C bus on SDA=GPIO 8, SCL=GPIO 9...");
  byte count = 0;
  for (byte i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" -> Found I2C device at address: 0x%02X", i);
      if (i == 0x3C) Serial.print(" (Default SSD1306 OLED)");
      if (i == 0x3D) Serial.print(" (Alternate SSD1306 OLED)");
      Serial.println();
      count++;
    }
  }
  if (count == 0) {
    Serial.println("[WARNING] No I2C devices found! Check your SDA (GPIO 8) and SCL (GPIO 9) wiring and 3.3V power.");
  } else {
    Serial.printf("[I2C] Scan complete. Found %d device(s).\n\n", count);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=======================================================");
  Serial.println("   ESPERDYNE BAT DETECTOR - TEST 02: 128x32 OLED TEST  ");
  Serial.println("=======================================================");

  // Initialize I2C with ESP32-S3 specific pins
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // Scan bus
  scanI2CBus();

  // Initialize display
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[FAIL] SSD1306 allocation failed! Attempting address 0x3D...");
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("[FAIL] Could not initialize OLED at 0x3C or 0x3D. Halt.");
      while (true) delay(100);
    }
  }

  Serial.println("[PASS] OLED initialized successfully!");

  // Stage 1: Splash Screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 4);
  display.println("ESPERDYNE PAM");
  display.setCursor(12, 18);
  display.println("OLED Test OK [128x32]");
  display.display();
  delay(2500);

  // Stage 2: Display border & diagonal line test (check pixel boundaries)
  display.clearDisplay();
  display.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SSD1306_WHITE);
  display.drawLine(0, 0, OLED_WIDTH - 1, OLED_HEIGHT - 1, SSD1306_WHITE);
  display.drawLine(0, OLED_HEIGHT - 1, OLED_WIDTH - 1, 0, SSD1306_WHITE);
  display.display();
  delay(1500);
}

int animFrame = 0;
float testFreq = 45.0; // 45 kHz bat frequency

void loop() {
  display.clearDisplay();

  // Top header bar (simulating Esperdyne PAM UI)
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.printf("kHz:%.1f", testFreq);

  display.setCursor(68, 0);
  display.print("Gain:50%");

  // Horizontal separator line
  display.drawLine(0, 10, OLED_WIDTH, 10, SSD1306_WHITE);

  // Bottom section: Mode, File index, and animated activity indicator
  display.setCursor(0, 16);
  display.print("Mode: ST [192k]");

  display.setCursor(0, 24);
  display.printf("REC: #%03d", animFrame % 100);

  // Moving bouncing activity block
  int boxX = 90 + ((animFrame * 2) % 32);
  display.fillRect(boxX, 18, 5, 10, SSD1306_WHITE);

  display.display();

  animFrame++;
  testFreq += 0.5;
  if (testFreq > 85.0) testFreq = 20.0;

  delay(100);
}
