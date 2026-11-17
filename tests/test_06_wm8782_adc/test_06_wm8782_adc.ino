/*
 * ==============================================================================
 * TEST 06: WM8782 I2S ADC 192kHz STEREO AUDIO CAPTURE & LIVE VU METER
 * ==============================================================================
 * ESP32-S3 acts as I2S SLAVE RX, while the WM8782 ADC operates as hardware
 * MASTER using its onboard 24.576 MHz crystal oscillator for ultra-low jitter!
 * 
 * ------------------------------------------------------------------------------
 * HARDWARE CONFIGURATION:
 * ------------------------------------------------------------------------------
 * 1. THE 2-POSITION YELLOW DIP SWITCH:
 *    - Switch 1 (M/S)  : Slide RIGHT -> 'Master' (towards gold crystal)
 *    - Switch 2 (Bits) : Slide RIGHT -> '24bit'  (towards gold crystal)
 * 
 * 2. THE ON-BOARD JUMPER:
 *    - Connect jumper between Pin 7 (MCLK) and Pin 8 (TO I2S 24.576M).
 *    - Leave top 3 pins (Pins 9, 10, 11) OPEN / NO JUMPERS (NC = 192 kHz).
 * 
 * 3. WIRING TO ESP32-S3 (11-PIN HEADER, COUNTED BOTTOM TO TOP):
 *    Pin #   ADC Pin Label       ESP32-S3 DevKit Pin
 *    ---------------------------------------------------------
 *    Pin 1   VCC=5V / VCC  ----> ESP32 3V3 (or 5Vin if IN-OUT bridged)
 *    Pin 2   GND           ----> ESP32 GND
 *    Pin 3   DGND          ----> ESP32 GND
 *    Pin 4   LRCK          ----> ESP32 GPIO 41
 *    Pin 5   DATA          ----> ESP32 GPIO 40
 *    Pin 6   BICK          ----> ESP32 GPIO 39
 *    Pin 7   MCLK          ----> Jumper to Pin 8 (below)
 *    Pin 8   TO I2S 24.5M  ----> Jumper to Pin 7 (above)
 *    Pins 9-11 (Top 3)     ----> LEAVE OPEN / NO WIRE
 * 
 * SERIAL MONITOR: 115200 Baud
 * ==============================================================================
 */

#include <Arduino.h>
#include "driver/i2s.h"

#define I2S_PORT        I2S_NUM_0
#define PIN_BICK        39
#define PIN_LRCK        41
#define PIN_DATA        40

#define SAMPLE_RATE     192000
#define READ_SAMPLES    512

// 32-bit slot for 24-bit stereo audio
int32_t rx_buffer[READ_SAMPLES * 2];

void setupI2S() {
  Serial.println("\n[I2S] Installing I2S0 Slave RX driver at 192 kHz...");

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_SLAVE | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = READ_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = PIN_BICK,
    .ws_io_num = PIN_LRCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PIN_DATA
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] Failed to install I2S driver: 0x%x\n", err);
    while (1) delay(100);
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] Failed to set I2S pins: 0x%x\n", err);
    while (1) delay(100);
  }

  i2s_zero_dma_buffer(I2S_PORT);
  Serial.println("[PASS] I2S Slave RX Ready. Listening for 192kHz audio stream!\n");
}

void printVUMeter(int level, int maxBars) {
  Serial.print("[");
  for (int i = 0; i < maxBars; i++) {
    if (i < level) Serial.print("=");
    else Serial.print(" ");
  }
  Serial.print("]");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n=======================================================");
  Serial.println("  ESPERDYNE WM8782 192kHz STEREO ADC LIVE AUDIO TEST  ");
  Serial.println("=======================================================");
  Serial.println("ADC Clock Master: 24.576 MHz Crystal (192 kHz @ 128fs)");
  Serial.println("ESP32 I2S Slave : GPIO 39 (BICK), 41 (LRCK), 40 (DATA)");
  Serial.println("-------------------------------------------------------");

  setupI2S();
}

unsigned long lastPrintTime = 0;

void loop() {
  size_t bytes_read = 0;
  esp_err_t res = i2s_read(I2S_PORT, rx_buffer, sizeof(rx_buffer), &bytes_read, portMAX_DELAY);

  if (res == ESP_OK && bytes_read > 0) {
    int samples_count = bytes_read / sizeof(int32_t); // Total L + R samples
    int stereo_pairs = samples_count / 2;

    int32_t minL = 2147483647, maxL = -2147483648;
    int32_t minR = 2147483647, maxR = -2147483648;

    for (int i = 0; i < stereo_pairs; i++) {
      // In 24-bit audio in 32-bit slot, shift right by 8 to get signed 24-bit range
      int32_t leftSample  = rx_buffer[i * 2]     >> 8;
      int32_t rightSample = rx_buffer[i * 2 + 1] >> 8;

      if (leftSample < minL)  minL = leftSample;
      if (leftSample > maxL)  maxL = leftSample;
      if (rightSample < minR) minR = rightSample;
      if (rightSample > maxR) maxR = rightSample;
    }

    int32_t p2pL = maxL - minL;
    int32_t p2pR = maxR - minR;

    // Rate-limit display output to ~20 fps (every 50 ms) for smooth readable terminal
    if (millis() - lastPrintTime >= 50) {
      lastPrintTime = millis();

      // Normalize p2p to 0..25 bars (using log scale / sensitivity scaling)
      int barsL = 0;
      int barsR = 0;
      if (p2pL > 50)  barsL = map(constrain(p2pL, 50, 1000000), 50, 1000000, 1, 24);
      if (p2pR > 50)  barsR = map(constrain(p2pR, 50, 1000000), 50, 1000000, 1, 24);

      Serial.print("L: ");
      printVUMeter(barsL, 24);
      Serial.printf(" %7d | R: ", p2pL);
      printVUMeter(barsR, 24);
      Serial.printf(" %7d | Raw0: [0x%08X, 0x%08X]\n", p2pR, rx_buffer[0], rx_buffer[1]);
    }
  }
}
