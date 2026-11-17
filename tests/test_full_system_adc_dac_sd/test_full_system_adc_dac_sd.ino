/*
 * ==============================================================================
 * ESPERDYNE BAT DETECTOR - FULL 2-LAYER HARDWARE & ULTRASONIC SYSTEM TEST
 * ==============================================================================
 * Incorporates ALL components across Layer 1 & Layer 2:
 *  - ESP32-S3 (8MB OPI PSRAM Ring Buffer + Dual Core FreeRTOS)
 *  - Layer 2: WM8782 192kHz 24-bit I2S ADC (BICK=39, DATA=40, LRCK=41)
 *  - Layer 2: Ultrasonic Knowles MEMS Mics (SPV0142LR5H-1)
 *  - Layer 1: PCM5102A I2S DAC Headphones (BCK=15, DIN=16, LCK=17)
 *  - Layer 1: 0.91" 128x32 I2C OLED (SDA=8, SCL=9)
 *  - Layer 1: MicroSD Card Logger (CS=10, MOSI=11, SCK=12, MISO=13)
 *  - Layer 1: EC11 Rotary Encoder + Push Switch (CLK=6, DT=5, SW=4)
 * 
 * CONTROLS (Rotary Encoder + Onboard BOOT Button):
 *  - TURN KNOB         : Adjust active parameter (Heterodyne Freq / Gain / Volume)
 *  - SHORT CLICK KNOB  : Cycle active parameter [HET -> GAIN -> VOL]
 *  - LONG PRESS KNOB   : Save last 2 seconds of 192kHz Stereo Audio to SD (.WAV)
 *    (or click ESP32 'BOOT' button on GPIO 0 to save WAV instantly!)
 * 
 * HETERODYNE FREQUENCY MODES:
 *  - 0 kHz (DIRECT) : Hear normal voice, snaps, & music straight through headphones!
 *  - 10 - 90 kHz    : Real-time ultrasonic heterodyne down-conversion!
 *                     (Set to ~40 kHz and shake keys / snap fingers near the mic!)
 * 
 * SERIAL MONITOR: 115200 Baud
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
// SD CARD COMPATIBILITY LAYER (SdFat or Standard SD.h)
// ------------------------------------------------------------------------------
#if __has_include(<SdFat.h>)
  #include <SdFat.h>
  #define USE_SDFAT 1
  SdFat sd;
  SdFile wavFile;
#else
  #include <SD.h>
  #include <FS.h>
  #define USE_SDFAT 0
  File wavFile;
#endif

// ------------------------------------------------------------------------------
// PIN DEFINITIONS (VERIFIED 2-LAYER HARDWARE MAP)
// ------------------------------------------------------------------------------
// Layer 1: OLED I2C
#define OLED_WIDTH        128
#define OLED_HEIGHT       32
#define OLED_RESET        -1
#define OLED_ADDR         0x3C
#define PIN_I2C_SDA       8
#define PIN_I2C_SCL       9

// Layer 1: EC11 Rotary Encoder & Onboard BOOT button
#define PIN_ENC_CLK       6
#define PIN_ENC_DT        5
#define PIN_ENC_SW        4
#define PIN_BOOT_BTN      0

// Layer 1: MicroSD SPI
#define PIN_SD_CS         10
#define PIN_SD_MOSI       11
#define PIN_SD_SCK        12
#define PIN_SD_MISO       13

// Layer 1: PCM5102A DAC (I2S_NUM_1 Master TX @ 48 kHz)
#define I2S_DAC_PORT      I2S_NUM_1
#define PIN_DAC_BCK       15
#define PIN_DAC_DIN       16
#define PIN_DAC_LCK       17
#define DAC_SAMPLE_RATE   48000

// Layer 2: WM8782 ADC (I2S_NUM_0 Slave RX @ 192 kHz)
#define I2S_ADC_PORT      I2S_NUM_0
#define PIN_ADC_BICK      39
#define PIN_ADC_DATA      40
#define PIN_ADC_LRCK      41
#define ADC_SAMPLE_RATE   192000

// Audio Buffer Framing (Exact 4:1 ratio: 512 frames @ 192k -> 128 frames @ 48k)
#define ADC_READ_FRAMES   512
#define DAC_OUT_FRAMES    (ADC_READ_FRAMES / 4)

// PSRAM Ring Buffer (2 seconds @ 192 kHz Stereo 16-bit = 1,536,000 bytes)
#define RING_SECONDS      2
#define RING_FRAMES       (ADC_SAMPLE_RATE * RING_SECONDS)
#define RING_SAMPLES      (RING_FRAMES * 2) // Stereo

// ------------------------------------------------------------------------------
// GLOBAL STATE & AUDIO BUFFERS
// ------------------------------------------------------------------------------
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

bool oledFound = false;
bool sdMounted = false;
uint32_t sdCapacityMB = 0;
volatile bool adcClockActive = false;
volatile uint32_t actualSampleRate = 0;

// PSRAM Circular Ring Buffer
int16_t* psramRingBuf = nullptr;
volatile uint32_t ringWriteIdx = 0;
volatile bool isSavingWav = false;
int wavFileCounter = 1;

// Real-time DSP & UI Parameters
enum ParamSelect { PARAM_HET = 0, PARAM_GAIN = 1, PARAM_VOL = 2 };
ParamSelect activeParam = PARAM_HET;

volatile int g_hetFreqKHz = 0;      // 0 = DIRECT (Audible), 10..90 = Ultrasonic Het
volatile int g_micGain = 32;        // Digital Preamp Gain: 1, 2, 4, 8, 16, 32, 64, 128
volatile int g_volume = 80;         // Headphone Volume: 0..100%

// Live Peak VU Levels (0..100)
volatile int g_vuLeft = 0;
volatile int g_vuRight = 0;

// Fast 1024-point Sine Lookup Table for Ultrasonic Heterodyne LO
float sinLUT[1024];

// DMA Buffers
int32_t adcDmaBuf[ADC_READ_FRAMES * 2];
int16_t dacDmaBuf[DAC_OUT_FRAMES * 2];

// Encoder State
int encoderTicks = 0;
uint8_t lastEncState = 0;
const int TICKS_PER_DETENT = 4;
bool lastSwState = HIGH;
unsigned long swPressStart = 0;
bool swLongPressHandled = false;
bool lastBootState = HIGH;

// ------------------------------------------------------------------------------
// DUAL SERIAL LOGGER (Works on both COM and USB ports)
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
// WAV FILE HEADER WRITER (192 kHz, 16-bit, Stereo PCM)
// ------------------------------------------------------------------------------
struct __attribute__((packed)) WavHeader {
  char riff[4] = {'R','I','F','F'};
  uint32_t fileSize;
  char wave[4] = {'W','A','V','E'};
  char fmt[4]  = {'f','m','t',' '};
  uint32_t fmtSize = 16;
  uint16_t audioFormat = 1; // PCM
  uint16_t numChannels = 2; // Stereo
  uint32_t sampleRate = ADC_SAMPLE_RATE;
  uint32_t byteRate   = ADC_SAMPLE_RATE * 2 * 2;
  uint16_t blockAlign = 4;
  uint16_t bitsPerSample = 16;
  char data[4] = {'d','a','t','a'};
  uint32_t dataSize;
};

// ------------------------------------------------------------------------------
// I2S HARDWARE DRIVERS SETUP
// ------------------------------------------------------------------------------
void setupI2S_ADC() {
  i2s_config_t adc_config = {
    .mode = (i2s_mode_t)(I2S_MODE_SLAVE | I2S_MODE_RX),
    .sample_rate = ADC_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = ADC_READ_FRAMES,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t adc_pins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = PIN_ADC_BICK,
    .ws_io_num = PIN_ADC_LRCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PIN_ADC_DATA
  };

  if (i2s_driver_install(I2S_ADC_PORT, &adc_config, 0, NULL) == ESP_OK) {
    i2s_set_pin(I2S_ADC_PORT, &adc_pins);
    i2s_zero_dma_buffer(I2S_ADC_PORT);
    logMsg("[PASS] WM8782 ADC I2S0 Slave RX Driver Installed (39/40/41)\n");
  } else {
    logMsg("[FAIL] ADC I2S0 Install Error!\n");
  }
}

void setupI2S_DAC() {
  i2s_config_t dac_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = DAC_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = DAC_OUT_FRAMES,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };

  i2s_pin_config_t dac_pins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = PIN_DAC_BCK,
    .ws_io_num = PIN_DAC_LCK,
    .data_out_num = PIN_DAC_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  if (i2s_driver_install(I2S_DAC_PORT, &dac_config, 0, NULL) == ESP_OK) {
    i2s_set_pin(I2S_DAC_PORT, &dac_pins);
    i2s_zero_dma_buffer(I2S_DAC_PORT);
    logMsg("[PASS] PCM5102A DAC I2S1 Master TX Driver Installed (15/16/17)\n");
  } else {
    logMsg("[FAIL] DAC I2S1 Install Error!\n");
  }
}

// ------------------------------------------------------------------------------
// CORE 1 REAL-TIME AUDIO DSP TASK (192kHz ADC -> HET -> 48kHz DAC + PSRAM RING)
// ------------------------------------------------------------------------------
void audioDspTask(void* parameter) {
  // DC-blocker filter states (removes mic bias offset)
  float prevRawL = 0.0f, hpL = 0.0f;
  float prevRawR = 0.0f, hpR = 0.0f;
  float lpfOut = 0.0f;

  // Heterodyne 32-bit Phase Accumulator
  uint32_t phaseAcc = 0;

  // Sample Rate Counter
  uint32_t framesCounted = 0;
  unsigned long rateTimer = millis();

  while (true) {
    if (isSavingWav) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    size_t bytes_read = 0;
    // Wait up to 80ms for WM8782 clock data (prevents hanging if ADC clock stops)
    esp_err_t res = i2s_read(I2S_ADC_PORT, adcDmaBuf, sizeof(adcDmaBuf), &bytes_read, pdMS_TO_TICKS(80));

    if (res != ESP_OK || bytes_read == 0) {
      adcClockActive = false;
      g_vuLeft = 0;
      g_vuRight = 0;

      // Output silence to DAC
      memset(dacDmaBuf, 0, sizeof(dacDmaBuf));
      size_t written = 0;
      i2s_write(I2S_DAC_PORT, dacDmaBuf, sizeof(dacDmaBuf), &written, pdMS_TO_TICKS(20));
      continue;
    }

    adcClockActive = true;
    int totalFrames = bytes_read / (2 * sizeof(int32_t)); // Stereo pairs received
    framesCounted += totalFrames;

    if (millis() - rateTimer >= 1000) {
      actualSampleRate = framesCounted;
      framesCounted = 0;
      rateTimer = millis();
    }

    int currentHetKHz = g_hetFreqKHz;
    uint32_t phaseStep = 0;
    if (currentHetKHz > 0) {
      double freqHz = (double)currentHetKHz * 1000.0;
      phaseStep = (uint32_t)((freqHz / (double)ADC_SAMPLE_RATE) * 4294967296.0);
    }

    float gain = (float)g_micGain;
    float volFactor = (float)g_volume / 100.0f;
    float peakL = 0.0f, peakR = 0.0f;

    int outIdx = 0;

    // Process in blocks of 4 frames (192 kHz -> 48 kHz decimation)
    for (int f = 0; f + 3 < totalFrames; f += 4) {
      float decimSum = 0.0f;

      for (int k = 0; k < 4; k++) {
        int idx = f + k;

        // WM8782 sends 24-bit left-justified in 32-bit slot -> shift right by 8
        float rawL = (float)(adcDmaBuf[idx * 2] >> 8);
        float rawR = (float)(adcDmaBuf[idx * 2 + 1] >> 8);

        // Save 16-bit raw ultrasonic stereo audio into PSRAM circular ring buffer
        if (psramRingBuf != nullptr) {
          psramRingBuf[ringWriteIdx]     = (int16_t)(adcDmaBuf[idx * 2] >> 16);
          psramRingBuf[ringWriteIdx + 1] = (int16_t)(adcDmaBuf[idx * 2 + 1] >> 16);
          ringWriteIdx = (ringWriteIdx + 2) % RING_SAMPLES;
        }

        // High-pass DC Blocker (cuts < 150 Hz rumble & DC offset)
        hpL = rawL - prevRawL + 0.995f * hpL;
        prevRawL = rawL;

        hpR = rawR - prevRawR + 0.995f * hpR;
        prevRawR = rawR;

        // Track Peak Amplitudes for VU meters
        float absL = fabsf(hpL);
        float absR = fabsf(hpR);
        if (absL > peakL) peakL = absL;
        if (absR > peakR) peakR = absR;

        // Combine L + R mics for monitoring (works with 1 or 2 mics connected!)
        float monoIn = (hpL + hpR) * 0.5f;

        // Heterodyne Down-Conversion Mixer
        if (currentHetKHz == 0) {
          decimSum += monoIn; // Direct Audible Pass-Through
        } else {
          float lo = sinLUT[phaseAcc >> 22]; // 10-bit LUT lookup
          phaseAcc += phaseStep;
          decimSum += (monoIn * lo * 2.0f);
        }
      }

      // Average 4 samples (Decimate 192 kHz -> 48 kHz) & Low-Pass Filter
      float avg = decimSum * 0.25f;
      lpfOut = lpfOut + 0.55f * (avg - lpfOut);

      // Map 24-bit down to 16-bit with digital preamp gain & headphone volume
      float outFloat = (lpfOut / 256.0f) * gain * volFactor;

      // Soft Clamp / Limiter to 16-bit range
      if (outFloat > 32700.0f)  outFloat = 32700.0f;
      if (outFloat < -32700.0f) outFloat = -32700.0f;

      int16_t dacSample = (int16_t)outFloat;
      if (outIdx < DAC_OUT_FRAMES) {
        dacDmaBuf[outIdx * 2]     = dacSample;
        dacDmaBuf[outIdx * 2 + 1] = dacSample;
        outIdx++;
      }
    }

    // Stream 48 kHz audible/heterodyne output to PCM5102A DAC
    // Use short timeout to avoid stalling the 192kHz ADC DMA due to crystal ppm drift
    if (outIdx > 0) {
      size_t bytes_written = 0;
      i2s_write(I2S_DAC_PORT, dacDmaBuf, outIdx * 2 * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(10));
    }

    // Normalize VU Meter (Log-like perceptual scaling for 24-bit input)
    int vuL = 0, vuR = 0;
    if (peakL > 250.0f) vuL = constrain((int)(log10f(peakL / 200.0f) * 28.0f), 0, 100);
    if (peakR > 250.0f) vuR = constrain((int)(log10f(peakR / 200.0f) * 28.0f), 0, 100);
    g_vuLeft  = vuL;
    g_vuRight = vuR;
  }
}

// ------------------------------------------------------------------------------
// SAVE PSRAM RING BUFFER AS 192 kHz STEREO WAV FILE
// ------------------------------------------------------------------------------
void saveRingBufferToSD() {
  if (!sdMounted) {
    logMsg("[WARN] Cannot save WAV: No SD card mounted!\n");
    return;
  }
  if (psramRingBuf == nullptr) {
    logMsg("[WARN] Cannot save WAV: PSRAM Ring Buffer not allocated!\n");
    return;
  }

  isSavingWav = true; // Pause audio task briefly for clean snapshot
  delay(20);

  if (oledFound) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 6);
    display.print("SAVING 192kHz WAV");
    display.setCursor(22, 18);
    display.printf("BAT_REC_%03d.WAV", wavFileCounter);
    display.display();
  }

  char filename[32];
  snprintf(filename, sizeof(filename), "/BAT_REC_%03d.WAV", wavFileCounter++);

  WavHeader hdr;
  uint32_t dataBytes = RING_SAMPLES * sizeof(int16_t);
  hdr.dataSize = dataBytes;
  hdr.fileSize = dataBytes + sizeof(WavHeader) - 8;

  uint32_t readStart = ringWriteIdx; // Oldest sample in circular buffer
  bool ok = false;

  logMsgf("[SD] Writing 2-sec 192kHz Stereo WAV -> %s (%u bytes)...\n", filename, dataBytes);
  unsigned long tStart = millis();

#if USE_SDFAT
  if (wavFile.open(filename, O_WRONLY | O_CREAT | O_TRUNC)) {
    wavFile.write((uint8_t*)&hdr, sizeof(WavHeader));
    // Write in two chunks if wrapped around circular buffer
    uint32_t firstChunkSamples = RING_SAMPLES - readStart;
    if (firstChunkSamples > 0) {
      wavFile.write((uint8_t*)&psramRingBuf[readStart], firstChunkSamples * sizeof(int16_t));
    }
    if (readStart > 0) {
      wavFile.write((uint8_t*)&psramRingBuf[0], readStart * sizeof(int16_t));
    }
    wavFile.sync();
    wavFile.close();
    ok = true;
  }
#else
  wavFile = SD.open(filename, FILE_WRITE);
  if (wavFile) {
    wavFile.write((uint8_t*)&hdr, sizeof(WavHeader));
    uint32_t firstChunkSamples = RING_SAMPLES - readStart;
    if (firstChunkSamples > 0) {
      wavFile.write((uint8_t*)&psramRingBuf[readStart], firstChunkSamples * sizeof(int16_t));
    }
    if (readStart > 0) {
      wavFile.write((uint8_t*)&psramRingBuf[0], readStart * sizeof(int16_t));
    }
    wavFile.close();
    ok = true;
  }
#endif

  unsigned long dur = millis() - tStart;
  if (ok) {
    logMsgf("[PASS] Saved %s in %lu ms!\n", filename, dur);
  } else {
    logMsg("[FAIL] Error writing WAV to SD!\n");
  }

  isSavingWav = false;
}

// ------------------------------------------------------------------------------
// OLED RENDER (128x32 LIVE TELEMETRY & VU METERS)
// ------------------------------------------------------------------------------
void renderOLED() {
  if (!oledFound) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // --- ROW 1 (y=0): Interactive Parameter Bar ---
  // Parameter 1: Heterodyne Frequency
  if (activeParam == PARAM_HET) {
    display.fillRect(0, 0, 48, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(1, 1);
  if (g_hetFreqKHz == 0) display.print("DIRECT ");
  else display.printf("HET:%2dk", g_hetFreqKHz);

  // Parameter 2: Mic Gain
  if (activeParam == PARAM_GAIN) {
    display.fillRect(50, 0, 35, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(52, 1);
  display.printf("G:%dx", g_micGain);

  // Parameter 3: Volume
  if (activeParam == PARAM_VOL) {
    display.fillRect(88, 0, 40, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(90, 1);
  display.printf("V:%3d%%", g_volume);

  display.setTextColor(SSD1306_WHITE);

  // --- ROWS 2 & 3: ADC Status & Live L/R VU Bars ---
  if (!adcClockActive) {
    display.setCursor(0, 13);
    display.print("ADC: NO I2S CLOCK!");
    display.setCursor(0, 23);
    display.print("Chk 3V3 / MCLK Jump");
  } else {
    // Left Channel VU Bar
    display.setCursor(0, 12);
    display.print("L");
    display.drawRect(8, 12, 88, 7, SSD1306_WHITE);
    int fillL = map(g_vuLeft, 0, 100, 0, 86);
    if (fillL > 0) display.fillRect(9, 13, fillL, 5, SSD1306_WHITE);

    // Right Channel VU Bar
    display.setCursor(0, 22);
    display.print("R");
    display.drawRect(8, 22, 88, 7, SSD1306_WHITE);
    int fillR = map(g_vuRight, 0, 100, 0, 86);
    if (fillR > 0) display.fillRect(9, 23, fillR, 5, SSD1306_WHITE);

    // Sample rate & SD status badge on right column
    display.setCursor(99, 12);
    display.printf("%3dk", (int)(actualSampleRate / 1000));
    display.setCursor(99, 22);
    display.print(sdMounted ? "SD:OK" : "NO SD");
  }

  display.display();
}

// ------------------------------------------------------------------------------
// SETUP
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial0.begin(115200);
  delay(1200);

  logMsg("\n=======================================================\n");
  logMsg("  ESPERDYNE BAT DETECTOR - FULL SYSTEM TEST (L1 + L2)  \n");
  logMsg("=======================================================\n");

  // 1. Precompute 1024-entry Heterodyne Sine LUT
  for (int i = 0; i < 1024; i++) {
    sinLUT[i] = sinf((2.0f * M_PI * (float)i) / 1024.0f);
  }

  // 2. Allocate 192 kHz Stereo Ring Buffer in PSRAM
  if (psramInit()) {
    size_t ringBytes = RING_SAMPLES * sizeof(int16_t);
    psramRingBuf = (int16_t*)heap_caps_malloc(ringBytes, MALLOC_CAP_SPIRAM);
    if (psramRingBuf != nullptr) {
      memset(psramRingBuf, 0, ringBytes);
      logMsgf("[PASS] PSRAM Ring Buffer Allocated: %.2f MB (2s @ 192kHz Stereo)\n",
              (float)ringBytes / (1024.0 * 1024.0));
    } else {
      logMsg("[WARN] Failed to allocate PSRAM Ring Buffer.\n");
    }
  } else {
    logMsg("[WARN] PSRAM not detected (Check Tools -> OPI PSRAM).\n");
  }

  // 3. Setup Controls (Encoder + Onboard BOOT Button)
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  lastEncState = (digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT);

  // 4. Initialize OLED Display (with Bus Probe)
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setTimeOut(30);
  Wire.beginTransmission(OLED_ADDR);
  if (Wire.endTransmission() == 0 && display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledFound = true;
    logMsg("[PASS] 0.91\" OLED Initialized.\n");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 4);
    display.print("ESPERDYNE BAT NODE");
    display.setCursor(16, 18);
    display.print("INIT 192kHz DSP...");
    display.display();
  } else {
    logMsg("[WARN] OLED not found at 0x3C.\n");
  }

  // 5. Initialize MicroSD Card
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
#if USE_SDFAT
  if (sd.begin(SdSpiConfig(PIN_SD_CS, DEDICATED_SPI, SD_SCK_MHZ(20)))) {
    sdMounted = true;
    sdCapacityMB = sd.card()->sectorCount() * 512UL / (1024UL * 1024UL);
    logMsgf("[PASS] MicroSD Mounted (SdFat): %u MB\n", sdCapacityMB);
  }
#else
  if (SD.begin(PIN_SD_CS, SPI, 20000000)) {
    sdMounted = true;
    sdCapacityMB = SD.cardSize() / (1024 * 1024);
    logMsgf("[PASS] MicroSD Mounted (SD.h): %u MB\n", sdCapacityMB);
  }
#endif
  if (!sdMounted) logMsg("[INFO] MicroSD not detected (Live audio will still work).\n");

  // 6. Initialize ADC (I2S0) & DAC (I2S1)
  setupI2S_DAC();
  setupI2S_ADC();

  // 7. Launch Real-Time Core 1 Audio DSP Task
  xTaskCreatePinnedToCore(
    audioDspTask,
    "AudioDSP",
    8192,
    NULL,
    configMAX_PRIORITIES - 2,
    NULL,
    1
  );

  logMsg("=======================================================\n");
  logMsg("CONTROLS:\n");
  logMsg(" * Turn Knob     : Tune Heterodyne (0kHz=DIRECT, 10-90kHz=BAT)\n");
  logMsg(" * Short Click   : Switch [HET -> GAIN -> VOL]\n");
  logMsg(" * Long Press / BOOT : Save 2-sec 192kHz WAV to SD card!\n");
  logMsg("=======================================================\n\n");
}

// ------------------------------------------------------------------------------
// MAIN LOOP (UI, ENCODER, TELEMETRY)
// ------------------------------------------------------------------------------
void loop() {
  bool needsRedraw = false;

  // 1. Read Quadrature Encoder
  uint8_t currentState = (digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT);
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

      if (activeParam == PARAM_HET) {
        if (g_hetFreqKHz == 0 && step > 0) g_hetFreqKHz = 15;
        else if (g_hetFreqKHz == 15 && step < 0) g_hetFreqKHz = 0;
        else g_hetFreqKHz += step * 2;

        if (g_hetFreqKHz < 0)  g_hetFreqKHz = 0;
        if (g_hetFreqKHz > 90) g_hetFreqKHz = 90;
        logMsgf("[HET] %s (%d kHz)\n", g_hetFreqKHz == 0 ? "DIRECT AUDIBLE" : "ULTRASONIC", g_hetFreqKHz);
      }
      else if (activeParam == PARAM_GAIN) {
        if (step > 0 && g_micGain < 128) g_micGain *= 2;
        if (step < 0 && g_micGain > 1)   g_micGain /= 2;
        logMsgf("[GAIN] %dx\n", g_micGain);
      }
      else if (activeParam == PARAM_VOL) {
        g_volume = constrain(g_volume + step * 5, 0, 100);
        logMsgf("[VOL] %d%%\n", g_volume);
      }
      needsRedraw = true;
    }
  }

  // 2. Encoder Push Switch (Short Click = Cycle Param, Long Press >800ms = Save WAV)
  bool swState = digitalRead(PIN_ENC_SW);
  if (swState == LOW && lastSwState == HIGH) {
    swPressStart = millis();
    swLongPressHandled = false;
  } else if (swState == LOW && !swLongPressHandled) {
    if (millis() - swPressStart > 800) {
      swLongPressHandled = true;
      saveRingBufferToSD();
      needsRedraw = true;
    }
  } else if (swState == HIGH && lastSwState == LOW) {
    if (!swLongPressHandled && (millis() - swPressStart > 35)) {
      activeParam = (ParamSelect)((activeParam + 1) % 3);
      needsRedraw = true;
    }
  }
  lastSwState = swState;

  // 3. Onboard ESP32 'BOOT' Button (Instant WAV Capture Trigger)
  bool bootState = digitalRead(PIN_BOOT_BTN);
  if (bootState == LOW && lastBootState == HIGH) {
    delay(30);
    if (digitalRead(PIN_BOOT_BTN) == LOW) {
      saveRingBufferToSD();
      needsRedraw = true;
    }
  }
  lastBootState = bootState;

  // 4. Refresh OLED & Serial VU Telemetry
  static unsigned long lastOledMs = 0;
  if (needsRedraw || (millis() - lastOledMs >= 50)) {
    lastOledMs = millis();
    renderOLED();
  }

  static unsigned long lastSerialMs = 0;
  if (millis() - lastSerialMs >= 250) {
    lastSerialMs = millis();
    if (adcClockActive) {
      logMsgf("[LIVE] Rate:%u Hz | Mode:%2dkHz | Gain:%dx | VU L:%3d%% R:%3d%%\n",
              actualSampleRate, g_hetFreqKHz, g_micGain, g_vuLeft, g_vuRight);
    } else {
      logMsg("[WAIT] Waiting for WM8782 ADC I2S clocks on GPIO 39/40/41...\n");
    }
  }
}
