/*
 * ==============================================================================
 * TEST 07: PCM5102A I2S DAC AUDIO OUTPUT & HEADPHONE TONE TEST
 * ==============================================================================
 * HARDWARE PINOUT / WIRING GUIDE:
 * 
 * ESP32-S3 DevKit Pin   <---->   PCM5102A I2S DAC Module Pin
 * -----------------------------------------------------------------
 *  5V (VIN) or 3V3*     <---->   VCC
 *  GND                  <---->   GND
 *  GND (CRITICAL!)      <---->   SCK  (Tying SCK to GND activates internal PLL)
 *  GPIO 15              <---->   BCK  (Bit Clock)
 *  GPIO 16              <---->   DIN  (Data Input)
 *  GPIO 17              <---->   LCK  / LRCK / WS (Left/Right Clock)
 *  3V3 (CRITICAL!**)    <---->   XMT  / MUTE (If your board has this pin on the header!)
 * 
 * *POWER NOTE:
 *  If your PCM5102A module has an onboard 3.3V regulator (small 5-pin chip), 
 *  connect VCC to 5V (VIN) for best headroom.
 * 
 * **THE "NO SOUND" HARDWARE CHECKLIST:
 * 1. Pin 'SCK' MUST BE CONNECTED TO GND. 
 *    If SCK is floating, the DAC has no system clock and remains 100% silent!
 * 2. Pin 'XMT' (Soft Mute):
 *    - If your board has an 'XMT' (or 'MUTE') pin header: connect it to 3.3V!
 *      (HIGH = Un-mute / Active; LOW/Floating = Muted).
 *    - If your board does NOT have an XMT header pin, check the solder jumpers
 *      on the back of the board (see detailed guide in Serial Monitor output).
 * 
 * SERIAL MONITOR: 115200 Baud
 * ==============================================================================
 */

#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>

#define I2S_OUT_PORT    I2S_NUM_1
#define I2S_OUT_BCK_IO  15
#define I2S_OUT_WS_IO   17
#define I2S_OUT_DO_IO   16

#define SAMPLE_RATE     48000 // Standard 48 kHz audio rate
#define BUFFER_SAMPLES  256

int16_t outBuffer[BUFFER_SAMPLES * 2]; // Stereo interleaved (L, R, L, R...)

void setupI2SDAC() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    // Standard Philips I2S format (MSB first with 1-bit delay)
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = BUFFER_SAMPLES,
    .use_apll = false, // ESP32-S3 uses PLL_F160M clock (APLL is for older ESP32)
    .tx_desc_auto_clear = true
  };

  i2s_pin_config_t pin_config = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = I2S_OUT_BCK_IO,
    .ws_io_num = I2S_OUT_WS_IO,
    .data_out_num = I2S_OUT_DO_IO,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err = i2s_driver_install(I2S_OUT_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[FAIL] I2S DAC Driver install failed! Error code: %d\n", err);
    return;
  }

  err = i2s_set_pin(I2S_OUT_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("[FAIL] I2S DAC Set Pin failed! Error code: %d\n", err);
    return;
  }

  i2s_zero_dma_buffer(I2S_OUT_PORT);
  Serial.println("[PASS] ESP32-S3 I2S1 DAC Output Driver Initialized Successfully!");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=======================================================");
  Serial.println("  ESPERDYNE BAT DETECTOR - TEST 07: PCM5102A I2S DAC   ");
  Serial.println("=======================================================");
  Serial.println("I2S Audio Output Pinout:");
  Serial.println(" - BCK  -> GPIO 15");
  Serial.println(" - DIN  -> GPIO 16");
  Serial.println(" - LCK  -> GPIO 17");
  Serial.println(" - SCK  -> GND (CRITICAL: enables internal PLL!)");
  Serial.println(" - VCC  -> 5V or 3.3V");
  Serial.println(" - GND  -> GND");
  Serial.println("-------------------------------------------------------");
  Serial.println("If no sound, check hardware MUTE:");
  Serial.println(" * If board has 'XMT' pin header -> connect XMT to 3.3V!");
  Serial.println(" * If board has solder jumpers on back: Jumper 3 (H3L) must be bridged to H (High)!");
  Serial.println("=======================================================\n");

  setupI2SDAC();
}

float toneFreq = 500.0; // 500 Hz test tone
float phase = 0.0;
unsigned long lastToneSwitch = 0;
int toneIndex = 0;
const float tones[] = { 440.0f, 880.0f, 1200.0f, 1760.0f };

void loop() {
  // Cycle musical frequencies every 1 second
  if (millis() - lastToneSwitch > 1000) {
    lastToneSwitch = millis();
    toneIndex = (toneIndex + 1) % 4;
    toneFreq = tones[toneIndex];
    Serial.printf("[AUDIO PLAYING] Tone: %.0f Hz (Stereo 16-bit, ~75%% Volume)\n", toneFreq);
  }

  float phaseIncrement = (2.0f * M_PI * toneFreq) / (float)SAMPLE_RATE;

  // Synthesize loud, clear sine wave (~75% full scale volume)
  for (int i = 0; i < BUFFER_SAMPLES; i++) {
    int16_t sample = (int16_t)(sin(phase) * 25000.0f); // 25000 out of 32767
    phase += phaseIncrement;
    if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;

    // Stereo: Left and Right channels interleaved
    outBuffer[i * 2]     = sample; // Left Channel
    outBuffer[i * 2 + 1] = sample; // Right Channel
  }

  // Stream directly to I2S DMA
  size_t bytes_written = 0;
  i2s_write(I2S_OUT_PORT, outBuffer, sizeof(outBuffer), &bytes_written, portMAX_DELAY);
}
