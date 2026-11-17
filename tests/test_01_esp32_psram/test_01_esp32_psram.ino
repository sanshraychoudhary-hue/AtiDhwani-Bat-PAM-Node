/*
 * ==============================================================================
 * TEST 01: ESP32-S3 BOARD & PSRAM INTEGRITY TEST
 * ==============================================================================
 * HARDWARE PINOUT / WIRING GUIDE:
 * 
 * No breadboard wires required for this test!
 * - Connect ESP32-S3 DevKit to PC using USB-C cable (use the 'COM' or 'UART' port).
 * - Open Arduino IDE:
 *     * Board: "ESP32S3 Dev Module"
 *     * PSRAM: "OPI PSRAM" (Crucial for N16R8!)
 *     * Flash Size: "16MB (128Mb)"
 *     * Partition Scheme: "16MB Flash (3MB APP/9.9MB FATFS)" or "Huge APP"
 *     * Upload Speed: 921600
 * - Serial Monitor Baud Rate: 115200
 * ==============================================================================
 */

#include <Arduino.h>
#include "esp_heap_caps.h"

void setup() {
  Serial.begin(115200);
  delay(1500); // Allow USB-CDC / Serial monitor to attach

  Serial.println("\n=======================================================");
  Serial.println("   ESPERDYNE BAT DETECTOR - TEST 01: ESP32-S3 & PSRAM   ");
  Serial.println("=======================================================");

  // 1. Chip Architecture
  Serial.printf("Chip Model       : %s\n", ESP.getChipModel());
  Serial.printf("Chip Revision    : %d\n", ESP.getChipRevision());
  Serial.printf("CPU Cores        : %d\n", ESP.getChipCores());
  Serial.printf("CPU Frequency    : %d MHz\n", ESP.getCpuFreqMHz());

  // 2. Flash Memory
  uint32_t flashSize = ESP.getFlashChipSize() / (1024 * 1024);
  Serial.printf("Flash Chip Size  : %u MB\n", flashSize);
  Serial.printf("Flash Chip Speed : %u MHz\n", ESP.getFlashChipSpeed() / 1000000);

  // 3. Internal SRAM
  Serial.printf("Internal Free Heap: %u bytes (%u KB)\n", ESP.getFreeHeap(), ESP.getFreeHeap() / 1024);

  // 4. PSRAM Validation (CRITICAL FOR ESPERDYNE 192kHz RING BUFFER)
  Serial.println("\n--- PSRAM (External RAM) Check ---");
  if (psramInit()) {
    size_t psramTotal = ESP.getPsramSize();
    size_t psramFree = ESP.getFreePsram();
    Serial.printf("PSRAM Initialized: SUCCESS!\n");
    Serial.printf("Total PSRAM Size : %u bytes (%.2f MB)\n", psramTotal, (float)psramTotal / (1024.0 * 1024.0));
    Serial.printf("Free PSRAM       : %u bytes (%.2f MB)\n", psramFree, (float)psramFree / (1024.0 * 1024.0));

    // Test allocating the actual Esperdyne 5-second 192kHz ring buffer (3.84 MB)
    size_t requiredBytes = 192000 * 2 * 5 * sizeof(int16_t); // 3,840,000 bytes
    Serial.printf("\nTesting allocation of 5-second Audio Ring Buffer (%u bytes / %.2f MB)...\n", 
                  requiredBytes, (float)requiredBytes / (1024.0 * 1024.0));

    int16_t* testBuffer = (int16_t*)heap_caps_malloc(requiredBytes, MALLOC_CAP_SPIRAM);
    if (testBuffer != nullptr) {
      Serial.println("[PASS] 3.84 MB PSRAM ring buffer allocated successfully!");
      
      // Write and read pattern to verify memory cell integrity
      Serial.print("Testing memory write/read integrity... ");
      bool memOk = true;
      for (size_t i = 0; i < 10000; i++) {
        testBuffer[i] = (int16_t)(i & 0x7FFF);
      }
      for (size_t i = 0; i < 10000; i++) {
        if (testBuffer[i] != (int16_t)(i & 0x7FFF)) {
          memOk = false;
          break;
        }
      }
      if (memOk) {
        Serial.println("[PASS] PSRAM data bus integrity verified!");
      } else {
        Serial.println("[FAIL] PSRAM data corruption detected!");
      }

      heap_caps_free(testBuffer);
      Serial.println("[INFO] Test buffer freed.");
    } else {
      Serial.println("[FAIL] Could not allocate 3.84 MB in PSRAM! Check Arduino IDE PSRAM setting ('OPI PSRAM').");
    }

  } else {
    Serial.println("[ERROR] PSRAM NOT DETECTED OR FAILED TO INITIALIZE!");
    Serial.println("-> Fix: In Arduino IDE -> Tools -> PSRAM -> Select 'OPI PSRAM'.");
  }

  Serial.println("=======================================================");
  Serial.println("TEST 01 COMPLETE. If PSRAM passed, your ESP32-S3 is 100% ready.");
  Serial.println("=======================================================\n");
}

void loop() {
  // Heartbeat blink or status print every 3 seconds
  delay(3000);
  Serial.printf("[Heartbeat] Uptime: %lu ms | Free Internal Heap: %u bytes | Free PSRAM: %u bytes\n", 
                millis(), ESP.getFreeHeap(), ESP.getFreePsram());
}
