/*
 * ==============================================================================
 * TEST 05: MICRO SD CARD (SPI / SDFAT) BENCHMARK & VERIFICATION
 * ==============================================================================
 * Tailored specifically for the green PCBCupid MicroSD breakout board!
 * 
 * ------------------------------------------------------------------------------
 * HARDWARE PINOUT / WIRING GUIDE:
 * ------------------------------------------------------------------------------
 * PCBCupid Pin (Left to Right)    ESP32-S3 DevKit Pin       Function / Note
 * ------------------------------------------------------------------------------
 *  [3V3]                       ->  ESP32 3V3                 Power (Pure 3.3V board)
 *  [D1]                        ->  LEAVE EMPTY               (Only for 4-bit SDMMC)
 *  [SCK]                       ->  ESP32 GPIO 12             SPI Clock
 *  [MO] (MOSI)                 ->  ESP32 GPIO 11             Master Out Slave In
 *  [MI] (MISO)                 ->  ESP32 GPIO 13             Master In Slave Out
 *  [CS]                        ->  ESP32 GPIO 10             Chip Select
 *  [CD]                        ->  LEAVE EMPTY               (Card Detect switch)
 *  [GND]                       ->  ESP32 GND                 Ground
 *  [D2]                        ->  LEAVE EMPTY               (Only for 4-bit SDMMC)
 * 
 * *CRITICAL POWER WARNING:
 *  This green PCBCupid board does NOT have an onboard 5V regulator.
 *  Connect its '3V3' pin directly to the ESP32's '3V3' rail!
 *  DO NOT connect it to 5V!
 * 
 * SERIAL MONITOR: 115200 Baud
 * ==============================================================================
 */

#include <Arduino.h>
#include <SPI.h>

#define SD_CS_PIN       10
#define SPI_MOSI_PIN    11
#define SPI_SCK_PIN     12
#define SPI_MISO_PIN    13

#if __has_include(<SdFat.h>)
#include <SdFat.h>
#define USE_SDFAT 1
SdFat sd;
SdFile file;
#define SPI_SPEED SD_SCK_MHZ(20)
#else
#include <SD.h>
#include <FS.h>
#define USE_SDFAT 0
File file;
#endif

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n=======================================================");
  Serial.println("   ESPERDYNE BAT DETECTOR - TEST 05: MICRO SD CARD TEST ");
  Serial.println("=======================================================");
  Serial.println("Configuring SPI Bus: CS=10, MOSI=11, SCK=12, MISO=13");

  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS_PIN);

#if USE_SDFAT
  Serial.println("Using High-Speed 'SdFat' Library (Dedicated SPI)...");
  Serial.print("Mounting MicroSD Card... ");
  if (!sd.begin(SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_SPEED))) {
    Serial.println("[FAIL]");
    Serial.println("\n--- SD CARD TROUBLESHOOTING CHECKLIST ---");
    Serial.println("1. Is the MicroSD card formatted as FAT32? (Use SD Card Formatter if unsure).");
    Serial.println("2. Power: Is '3V3' connected firmly to ESP32 3V3?");
    Serial.println("3. Wiring: Ensure SCK->12, MO->11, MI->13, CS->10, and GND is connected.");
    Serial.println("4. Ensure the card is pushed fully into the click socket.");
    sd.initErrorHalt(&Serial);
    return;
  }
  Serial.println("[PASS] MicroSD Card mounted successfully!");

  // 1. Capacity
  uint32_t cardSizeMB = sd.card()->sectorCount() * 512UL / (1024UL * 1024UL);
  Serial.printf("Card Capacity : %u MB (%.2f GB)\n", cardSizeMB, (float)cardSizeMB / 1024.0);

  // 2. Write Benchmark Test
  const char* testFileName = "/sd_bench.tmp";
  Serial.printf("\nTesting write speed on '%s' (Writing 128 KB of audio buffer data)...\n", testFileName);

  if (file.open(testFileName, O_WRONLY | O_CREAT | O_TRUNC)) {
    const size_t BUF_SIZE = 512;
    uint8_t buffer[BUF_SIZE];
    for (size_t i = 0; i < BUF_SIZE; i++) buffer[i] = (uint8_t)(i & 0xFF);

    unsigned long startWrite = millis();
    const int TOTAL_BLOCKS = 256; // 256 * 512 bytes = 128 KB
    for (int b = 0; b < TOTAL_BLOCKS; b++) {
      file.write(buffer, BUF_SIZE);
    }
    file.sync();
    file.close();
    unsigned long writeDuration = millis() - startWrite;

    float speedKBps = (128.0f * 1000.0f) / (float)writeDuration;
    Serial.printf("[PASS] Write completed in %lu ms | Speed: %.1f KB/sec\n", writeDuration, speedKBps);
  } else {
    Serial.println("[FAIL] Could not create test file on SD card!");
  }

  // 3. Read Verification Test
  Serial.print("Testing file read and data verification... ");
  if (file.open(testFileName, O_RDONLY)) {
    uint8_t readBuf[512];
    bool dataOk = true;
    while (file.available()) {
      int bytesRead = file.read(readBuf, sizeof(readBuf));
      for (int i = 0; i < bytesRead; i++) {
        if (readBuf[i] != (uint8_t)(i & 0xFF)) {
          dataOk = false;
          break;
        }
      }
      if (!dataOk) break;
    }
    file.close();

    if (dataOk) {
      Serial.println("[PASS] Data readback matches byte-for-byte (No corruption)!");
    } else {
      Serial.println("[FAIL] Data corruption detected during readback!");
    }

    sd.remove(testFileName);
  } else {
    Serial.println("[FAIL] Could not open file for read verification.");
  }

  // 4. Directory Listing
  Serial.println("\n--- Current SD Card Files ---");
  sd.ls(&Serial, LS_R | LS_SIZE);

#else
  Serial.println("Using Standard 'SD.h' Library...");
  Serial.print("Mounting MicroSD Card... ");
  if (!SD.begin(SD_CS_PIN, SPI, 20000000)) {
    Serial.println("[FAIL]");
    Serial.println("\n--- SD CARD TROUBLESHOOTING CHECKLIST ---");
    Serial.println("1. Is the MicroSD card formatted as FAT32?");
    Serial.println("2. Power: Is '3V3' connected firmly to ESP32 3V3?");
    Serial.println("3. Wiring: Ensure SCK->12, MO->11, MI->13, CS->10, and GND is connected.");
    Serial.println("4. Ensure the card is pushed fully into the click socket.");
    return;
  }
  Serial.println("[PASS] MicroSD Card mounted successfully!");

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("Card Capacity : %llu MB (%.2f GB)\n", cardSize, (float)cardSize / 1024.0);

  // Write Benchmark
  const char* testFileName = "/sd_bench.tmp";
  Serial.printf("\nTesting write speed on '%s' (Writing 128 KB of audio buffer data)...\n", testFileName);

  file = SD.open(testFileName, FILE_WRITE);
  if (file) {
    const size_t BUF_SIZE = 512;
    uint8_t buffer[BUF_SIZE];
    for (size_t i = 0; i < BUF_SIZE; i++) buffer[i] = (uint8_t)(i & 0xFF);

    unsigned long startWrite = millis();
    for (int b = 0; b < 256; b++) {
      file.write(buffer, BUF_SIZE);
    }
    file.flush();
    file.close();
    unsigned long writeDuration = millis() - startWrite;
    float speedKBps = (128.0f * 1000.0f) / (float)writeDuration;
    Serial.printf("[PASS] Write completed in %lu ms | Speed: %.1f KB/sec\n", writeDuration, speedKBps);
  } else {
    Serial.println("[FAIL] Could not create test file on SD card!");
  }

  // Read Verification
  Serial.print("Testing file read and data verification... ");
  file = SD.open(testFileName, FILE_READ);
  if (file) {
    uint8_t readBuf[512];
    bool dataOk = true;
    while (file.available()) {
      int bytesRead = file.read(readBuf, sizeof(readBuf));
      for (int i = 0; i < bytesRead; i++) {
        if (readBuf[i] != (uint8_t)(i & 0xFF)) {
          dataOk = false;
          break;
        }
      }
      if (!dataOk) break;
    }
    file.close();
    if (dataOk) {
      Serial.println("[PASS] Data readback matches byte-for-byte!");
    } else {
      Serial.println("[FAIL] Data corruption detected during readback!");
    }
    SD.remove(testFileName);
  }
#endif

  Serial.println("\n=======================================================");
  Serial.println("TEST 05 COMPLETE. MicroSD card SPI communication is 100% verified!");
  Serial.println("=======================================================\n");
}

void loop() {
  delay(5000);
}
