/*
 * ==============================================================================
 * ESPERDYNE BAT DETECTOR & PAM NODE - PRO EDITION FIRMWARE (v2.0)
 * ==============================================================================
 * Complete field-ready bioacoustics suite for your 2-Layer ESP32-S3 Zero-PCB:
 * 
 * HARDWARE:
 *  - MCU       : ESP32-S3-N16R8 (16MB Flash, 8MB OPI PSRAM, Dual-Core 240MHz)
 *  - Audio In  : WM8782 192kHz 24-bit Master ADC -> I2S0 Slave RX (39/40/41)
 *  - Audio Out : PCM5102A 32-bit DAC -> I2S1 Master TX @ 48kHz (15/16/17)
 *  - Display   : 0.91" 128x32 SSD1306 I2C OLED (SDA=8, SCL=9, 0x3C)
 *  - Storage   : MicroSD SPI (CS=10, MOSI=11, SCK=12, MISO=13)
 *  - Controls  : EC11 Rotary Knob (CLK=6, DT=5, SW=4) + ESP32 BOOT (GPIO 0)
 * 
 * PRO FEATURES INCLUDED:
 *  1. PAGE 1 (MAIN)     : Dual-Channel Heterodyne (10-90kHz / DR) & Gain up to 512x
 *                         + Broadband Frequency Division (FD /10) mode!
 *  2. PAGE 2 (SPECTRUM) : Real-time 256-pt FFT (15-90kHz) Analyzer + Peak kHz!
 *  3. PAGE 3 (PLAYER)   : MicroSD WAV Browser & 10x / 5x / 1x Time-Expansion Audio!
 *  4. PAGE 4 (SETTINGS) : Input Mic Selector (L+R/L/R), Auto-PAM Threshold Tuner,
 *                         Instant 2s Time-Expanded Ring Replay, & Wi-Fi Hotspot!
 *  5. WIFI COMPANION    : Built-in SoftAP ('Esperdyne-BatNode' @ 192.168.4.1) to
 *                         view live ultrasonic peak & download WAVs to your phone!
 *  6. 5s PSRAM RING     : Continuous 3.66MB circular ring buffer + Overnight AUTO-PAM!
 * 
 * QUICK CONTROLS:
 *  - 2x Double-Click Knob : Switch Page [MAIN -> SPECTRUM -> PLAYER -> SETTINGS]
 *  - Turn / 1x Click Knob : Adjust / Select within active page
 *  - Hold Knob (>0.7s)    : Save 5s WAV (Main/Spec) | Speed 10x/5x/1x (Player)
 *  - Tap 'BOOT' (GPIO 0)  : Save 5s WAV instantly from any page
 *  - Hold 'BOOT' (>0.7s)  : Toggle Overnight AUTO-PAM trigger mode
 * ==============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSerif9pt7b.h>
#include "driver/i2s.h"
#include "esp_heap_caps.h"
#include <math.h>

#include "config.h"
#include "fft_engine.h"
#include "web_companion.h"

// ------------------------------------------------------------------------------
// SD CARD DRIVER (Auto-selects SdFat if installed, else ESP32 SD.h)
// ------------------------------------------------------------------------------
#if __has_include(<SdFat.h>)
  #include <SdFat.h>
  #define USE_SDFAT 1
  SdFat sd;
  SdFile wavFile;
  SdFile playSdFile;
  #define SD_SPI_SPEED SD_SCK_MHZ(20)
#else
  #include <SD.h>
  #include <FS.h>
  #define USE_SDFAT 0
  File wavFile;
  File playSdFile;
#endif

// ------------------------------------------------------------------------------
// GLOBAL OBJECTS & SHARED STATE
// ------------------------------------------------------------------------------
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
UltrasonicFFT fftEngine;

bool oledPresent = false;
bool oledSleeping = false;
unsigned long lastUserActivityMs = 0;

bool sdMounted = false;
int nextFileIndex = 0;
uint32_t sdTotalMB = 0;

WavEntry wavList[MAX_WAV_LIST];
int wavListCount = 0;

volatile bool adcClockAlive = false;
volatile uint32_t measuredSampleRate = 0;

// PSRAM Buffers
int16_t* ringBuffer     = nullptr; // 5s continuous ultrasonic ring
int16_t* teSnapshotBuf  = nullptr; // 2s Time-Expansion live replay snapshot
int16_t* sdStreamBuf    = nullptr; // 256KiB SPSC buffer for SD WAV playback

volatile uint32_t ringWriteIdx = 0;
volatile uint32_t ringValidSamples = 0;
volatile bool isSavingWav = false;

// DSP & Mode Settings
UIPage currentPage = PAGE_MAIN;
EditField activeField = FIELD_F1;

volatile ListenMode g_listenMode       = LISTEN_HET;
volatile InputChannelMode g_inputCh    = IN_LR_STEREO;
volatile VolumeCurve g_volCurve        = VOL_CURVE_LOG; // Default to smooth logarithmic audio taper
volatile int g_freq1KHz                = 45;
volatile int g_freq2KHz                = 45;
volatile int g_volume                  = 30;            // Default comfortable, noise-free listening volume
volatile int g_micGain                 = 32;
volatile bool g_mixMode                = true;

inline float getEffectiveVolume() {
  float v = (float)g_volume / 100.0f;
  return (g_volCurve == VOL_CURVE_LOG) ? (v * v) : v;
}

// Playback Engine State
volatile PlaybackSource g_playSource   = SRC_LIVE_ADC;
volatile PlayerListenMode g_playerMode = PLAY_HET_1X; // Default: Real-time 1x HET + Spectrogram!
volatile int g_teSpeedFactor           = 10;          // 10x or 5x when in TE mode
volatile double teReadFramePos         = 0.0;
volatile uint32_t teTotalFrames        = 0;

volatile uint32_t sdStreamWriteFrame   = 0; // Written by Core 0
volatile uint32_t sdStreamReadFrame    = 0; // Read by Core 1
volatile double sdFracOffset           = 0.0;
volatile uint32_t sdFileSampleRate     = 192000;
volatile uint16_t sdFileChannels       = 2;
volatile uint32_t sdFileDataBytesRem   = 0;
volatile bool sdFileEof                = false;
uint32_t sdPlayTotalDataBytes          = 0;
uint32_t sdDataStartOffset             = 44;
bool sdLoopPlayback                    = true;
int playerSelectedIdx                  = 0;
bool playerIsActive                    = false;

// Live FFT Capture Handshake (Core 1 -> Core 0)
float fftCaptureBuf[FFT_SIZE];
volatile float fftSampleRateHz = 192000.0f;
volatile bool fftCaptureReady  = false;
float g_peakFreqKHz            = 0.0f;

// Telemetry & Auto-PAM
volatile int g_vuPeak = 0;
volatile float g_ultraPeak = 0.0f;
bool autoModeEnabled = false;
bool autoTriggerPending = false;
unsigned long autoTriggerTimeMs = 0;
unsigned long autoCooldownUntilMs = 0;
float g_trigThreshold = 4500.0f;

// Settings Menu State
int settingsCursor = 0;
bool settingsEditingThresh = false;
const int SETTINGS_COUNT = 11;

// Live Battery Telemetry (GPIO 1 via 1:2 Divider)
float g_batVolts = 4.05f;
int   g_batPercent = 90;
unsigned long g_lastBatCheckMs = 0;

// ADC Diagnostic & Signal Generator State (GPIO 18)
volatile int g_testGenFreqKHz        = 40;            // 35 to 100 kHz (default 40 kHz)
volatile TestGenState g_testGenState = TEST_SIG_ON;   // ON by default in test mode
TestField g_testActiveField          = TEST_FIELD_GEN;
unsigned long g_lastSweepStepMs      = 0;
int g_sweepDirection                 = 1;

// Fast 1024-pt Sine LUT & DMA Buffers
float sinLUT[1024];
int32_t adcDmaBuf[ADC_READ_FRAMES * 2];
int16_t dacDmaBuf[DAC_OUT_FRAMES * 2];

// Encoder & Button Tracking
int encoderTicks = 0;
uint8_t lastEncState = 0;
const int TICKS_PER_DETENT = 4;

bool swLastReading = HIGH;
unsigned long swDownMs = 0;
bool swHoldFired = false;
bool swWaitDouble = false;
unsigned long swLastClickUpMs = 0;

bool bootLastReading = HIGH;
unsigned long bootDownMs = 0;
bool bootHoldFired = false;

// ------------------------------------------------------------------------------
// DUAL SERIAL OUTPUT HELPER
// ------------------------------------------------------------------------------
void logPrint(const char* s) {
  Serial.print(s);
  Serial0.print(s);
}

void logPrintf(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
  Serial0.print(buf);
}

// ------------------------------------------------------------------------------
// HARDWARE ULTRASONIC SIGNAL GENERATOR (ESP32-S3 LEDC ON GPIO 18)
// ------------------------------------------------------------------------------
void setSignalGenOutput(bool enable, uint32_t freqKHz) {
  static bool attached = false;
  static uint32_t lastFreqHz = 0;

  if (!enable || freqKHz == 0) {
    if (attached) {
      ledcWrite(PIN_SIG_GEN, 0);
    }
    return;
  }

  uint32_t freqHz = freqKHz * 1000UL;
  if (!attached) {
    pinMode(PIN_SIG_GEN, OUTPUT);
    digitalWrite(PIN_SIG_GEN, LOW);
    ledcAttach(PIN_SIG_GEN, freqHz, 8);
    attached = true;
    lastFreqHz = freqHz;
    ledcWrite(PIN_SIG_GEN, 20); // ~8% duty cycle (~0.36V RMS fundamental)
  } else {
    if (lastFreqHz != freqHz) {
      ledcChangeFrequency(PIN_SIG_GEN, freqHz, 8);
      lastFreqHz = freqHz;
    }
    ledcWrite(PIN_SIG_GEN, 20);
  }
}

// ------------------------------------------------------------------------------
// SD CARD SCANNING, WAV HELPERS & STREAMING
// ------------------------------------------------------------------------------
static bool isWavExtension(const char* name) {
  if (!name) return false;
  size_t len = strlen(name);
  if (len >= 4 && strcasecmp(name + len - 4, ".wav") == 0) return true;
  if (len >= 5 && strcasecmp(name + len - 5, ".wave") == 0) return true;
  return false;
}

static bool isRecFileName(const char* name, int* outIdx) {
  if (!name || strncasecmp(name, "REC", 3) != 0) return false;
  const char* p = name + 3;
  if (!isdigit((unsigned char)*p)) return false;
  char* endPtr = nullptr;
  long val = strtol(p, &endPtr, 10);
  if (!endPtr || endPtr == p) return false;
  if (!isWavExtension(endPtr)) return false;
  if (outIdx) *outIdx = (int)val;
  return true;
}

static bool compareWavEntries(const WavEntry& a, const WavEntry& b) {
  int idxA = -1, idxB = -1;
  bool isRecA = isRecFileName(a.name, &idxA);
  bool isRecB = isRecFileName(b.name, &idxB);

  // 1. Both are custom / laptop files -> sort alphabetically (case-insensitive)
  if (!isRecA && !isRecB) {
    return strcasecmp(a.name, b.name) < 0;
  }
  // 2. Custom files appear before REC recordings for instant access
  if (!isRecA && isRecB) {
    return true;
  }
  if (isRecA && !isRecB) {
    return false;
  }
  // 3. Both are REC###.WAV files -> newest recording first (descending index)
  return idxA > idxB;
}

void scanSDWavFiles() {
  wavListCount = 0;
  int maxFoundRecIdx = 0;
  if (!sdMounted) return;

#if USE_SDFAT
  SdFile dir, entry;
  char name[64];
  if (dir.open("/")) {
    while (entry.openNext(&dir, O_RDONLY)) {
      if (entry.isFile()) {
        entry.getName(name, sizeof(name));
        // Skip hidden files, system folders and Mac resource forks (e.g. ._, .Trashes)
        if (name[0] != '.' && name[0] != '_' && strncmp(name, "._", 2) != 0) {
          if (isWavExtension(name)) {
            if (wavListCount < MAX_WAV_LIST) {
              strncpy(wavList[wavListCount].name, name, 63);
              wavList[wavListCount].name[63] = '\0';
              wavList[wavListCount].sizeBytes = entry.fileSize();
              wavListCount++;
            }
            int recIdx = 0;
            if (isRecFileName(name, &recIdx)) {
              if (recIdx > maxFoundRecIdx) maxFoundRecIdx = recIdx;
            }
          }
        }
      }
      entry.close();
    }
    dir.close();
  }
#else
  File dir = SD.open("/");
  if (dir) {
    while (File entry = dir.openNextFile()) {
      if (!entry.isDirectory()) {
        const char* name = entry.name();
        if (name[0] == '/') name++;
        // Skip hidden files, system folders and Mac resource forks (e.g. ._, .Trashes)
        if (name[0] != '.' && name[0] != '_' && strncmp(name, "._", 2) != 0) {
          if (isWavExtension(name)) {
            if (wavListCount < MAX_WAV_LIST) {
              strncpy(wavList[wavListCount].name, name, 63);
              wavList[wavListCount].name[63] = '\0';
              wavList[wavListCount].sizeBytes = entry.size();
              wavListCount++;
            }
            int recIdx = 0;
            if (isRecFileName(name, &recIdx)) {
              if (recIdx > maxFoundRecIdx) maxFoundRecIdx = recIdx;
            }
          }
        }
      }
      entry.close();
    }
    dir.close();
  }
#endif

  nextFileIndex = maxFoundRecIdx + 1;

  // Sort files: Custom files first (alphabetical), then newest recordings first
  for (int i = 0; i < wavListCount - 1; i++) {
    for (int j = i + 1; j < wavListCount; j++) {
      if (!compareWavEntries(wavList[i], wavList[j])) {
        WavEntry tmp = wavList[i];
        wavList[i] = wavList[j];
        wavList[j] = tmp;
      }
    }
  }

  if (playerSelectedIdx >= wavListCount) {
    playerSelectedIdx = max(0, wavListCount - 1);
  }
}

bool initSDCard() {
  sdMounted = false;
#if USE_SDFAT
  if (sd.begin(SdSpiConfig(PIN_SD_CS, DEDICATED_SPI, SD_SPI_SPEED))) {
    sdMounted = true;
    sdTotalMB = sd.card()->sectorCount() * 512UL / (1024UL * 1024UL);
    logPrintf("[PASS] MicroSD Mounted (SdFat): %u MB\n", sdTotalMB);
    scanSDWavFiles();
    return true;
  }
#else
  if (SD.begin(PIN_SD_CS, SPI, 20000000, "/sd", 10)) {
    sdMounted = true;
    sdTotalMB = SD.cardSize() / (1024 * 1024);
    logPrintf("[PASS] MicroSD Mounted (SD.h): %u MB\n", sdTotalMB);
    scanSDWavFiles();
    return true;
  }
#endif
  logPrint("[WARN] MicroSD not detected.\n");
  return false;
}

bool deleteWavFileFromSD(const char* filename) {
  if (!sdMounted) return false;
  char fullPath[72];
  if (filename[0] == '/') snprintf(fullPath, sizeof(fullPath), "%s", filename);
  else snprintf(fullPath, sizeof(fullPath), "/%s", filename);
#if USE_SDFAT
  bool res = sd.remove(fullPath);
#else
  bool res = SD.remove(fullPath);
#endif
  if (res) scanSDWavFiles();
  return res;
}

// Helper template avoids duplicating SdFat vs SD.h loops & compiles under C++11
struct SdFileHelper {
  template <typename FileT>
  static void writeRingChunks(FileT& fObj, const WavHeader& hdr, uint32_t startIdx, uint32_t firstPart, uint32_t remaining) {
    fObj.write((const uint8_t*)&hdr, sizeof(WavHeader));
    const uint32_t CHUNK_SAMPS = 4096;
    for (uint32_t pos = 0; pos < firstPart; pos += CHUNK_SAMPS) {
      uint32_t cnt = min(CHUNK_SAMPS, firstPart - pos);
      fObj.write((const uint8_t*)&ringBuffer[startIdx + pos], cnt * sizeof(int16_t));
    }
    for (uint32_t pos = 0; pos < remaining; pos += CHUNK_SAMPS) {
      uint32_t cnt = min(CHUNK_SAMPS, remaining - pos);
      fObj.write((const uint8_t*)&ringBuffer[pos], cnt * sizeof(int16_t));
    }
  }

  template <typename FileT>
  static uint32_t fileSize(FileT& fObj) {
#if USE_SDFAT
    return fObj.fileSize();
#else
    return fObj.size();
#endif
  }

  template <typename FileT>
  static void streamToHttp(FileT& fObj, WiFiClient& client, WebServer& srv, const char* cleanName) {
    uint32_t total = fileSize(fObj);
    srv.setContentLength(total);
    srv.sendHeader("Content-Disposition", String("attachment; filename=\"") + cleanName + "\"");
    srv.send(200, "audio/wav", "");
    uint8_t buf[4096];
    uint32_t sent = 0;
    while (sent < total && client.connected()) {
      int n = fObj.read(buf, min((uint32_t)sizeof(buf), total - sent));
      if (n <= 0) break;
      client.write(buf, n);
      sent += n;
      yield();
    }
  }
};

bool streamWavFileToClient(WiFiClient& client, const char* filename, WebServer& srv) {
  if (!sdMounted) return false;
  const char* cleanName = (filename[0] == '/') ? filename + 1 : filename;
#if USE_SDFAT
  if (!wavFile.open(filename, O_RDONLY)) return false;
  SdFileHelper::streamToHttp(wavFile, client, srv, cleanName);
  wavFile.close();
  return true;
#else
  wavFile = SD.open(filename, FILE_READ);
  if (!wavFile) return false;
  SdFileHelper::streamToHttp(wavFile, client, srv, cleanName);
  wavFile.close();
  return true;
#endif
}

// ------------------------------------------------------------------------------
// I2S SETUP: ADC (I2S0 SLAVE RX @ 192kHz) & DAC (I2S1 MASTER TX @ 48kHz)
// ------------------------------------------------------------------------------
void setupI2S_ADC() {
  i2s_config_t cfg = {
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
  i2s_pin_config_t pins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = PIN_ADC_BICK,
    .ws_io_num = PIN_ADC_LRCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PIN_ADC_DATA
  };
  if (i2s_driver_install(I2S_ADC_PORT, &cfg, 0, NULL) == ESP_OK) {
    i2s_set_pin(I2S_ADC_PORT, &pins);
    i2s_zero_dma_buffer(I2S_ADC_PORT);
    logPrint("[PASS] WM8782 ADC Ready (192 kHz Slave RX).\n");
  }
}

void setupI2S_DAC() {
  i2s_config_t cfg = {
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
  i2s_pin_config_t pins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = PIN_DAC_BCK,
    .ws_io_num = PIN_DAC_LCK,
    .data_out_num = PIN_DAC_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  if (i2s_driver_install(I2S_DAC_PORT, &cfg, 0, NULL) == ESP_OK) {
    i2s_set_pin(I2S_DAC_PORT, &pins);
    i2s_zero_dma_buffer(I2S_DAC_PORT);
    logPrint("[PASS] PCM5102A DAC Ready (48 kHz Master TX).\n");
  }
}

// ------------------------------------------------------------------------------
// SD WAV PLAYBACK CONTROL & SPSC STREAM REFILL (CORE 0 ONLY!)
// ------------------------------------------------------------------------------
void stopPlayback() {
  if (g_playSource == SRC_SD_WAV || g_playSource == SRC_TE_RING) {
    g_playSource = SRC_LIVE_ADC;
    delay(15);
  }
#if USE_SDFAT
  if (playSdFile.isOpen()) playSdFile.close();
#else
  if (playSdFile) playSdFile.close();
#endif
  playerIsActive = false;
}

bool startSDWavPlayback(const char* filename) {
  if (!sdMounted || sdStreamBuf == nullptr) return false;
  stopPlayback();

  char fullPath[72];
  if (filename[0] == '/') snprintf(fullPath, sizeof(fullPath), "%s", filename);
  else snprintf(fullPath, sizeof(fullPath), "/%s", filename);

#if USE_SDFAT
  if (!playSdFile.open(fullPath, O_RDONLY)) return false;
#else
  playSdFile = SD.open(fullPath, FILE_READ);
  if (!playSdFile) return false;
#endif

  // Parse RIFF WAV chunks cleanly (finds 'fmt ' and 'data')
  uint8_t hdr12[12];
  if (playSdFile.read(hdr12, 12) != 12 || memcmp(hdr12, "RIFF", 4) != 0 || memcmp(hdr12 + 8, "WAVE", 4) != 0) {
    playSdFile.close();
    return false;
  }

  uint32_t foundSR = 192000;
  uint16_t foundCh = 2;
  uint32_t dataBytes = 0;
  bool gotFmt = false, gotData = false;

  while (true) {
    uint8_t chunkHdr[8];
    if (playSdFile.read(chunkHdr, 8) != 8) break;
    uint32_t cSize = (uint32_t)chunkHdr[4] | ((uint32_t)chunkHdr[5] << 8) | ((uint32_t)chunkHdr[6] << 16) | ((uint32_t)chunkHdr[7] << 24);

    if (memcmp(chunkHdr, "fmt ", 4) == 0 && cSize >= 16) {
      uint8_t fmtData[16];
      playSdFile.read(fmtData, 16);
      foundCh = (uint16_t)fmtData[2] | ((uint16_t)fmtData[3] << 8);
      foundSR = (uint32_t)fmtData[4] | ((uint32_t)fmtData[5] << 8) | ((uint32_t)fmtData[6] << 16) | ((uint32_t)fmtData[7] << 24);
      if (cSize > 16) {
        // Skip remainder of fmt chunk
        uint32_t rem = cSize - 16;
        while (rem > 0) { uint8_t d; playSdFile.read(&d, 1); rem--; }
      }
      gotFmt = true;
    } else if (memcmp(chunkHdr, "data", 4) == 0) {
      dataBytes = cSize;
      gotData = true;
#if USE_SDFAT
      sdDataStartOffset = playSdFile.curPosition();
#else
      sdDataStartOffset = playSdFile.position();
#endif
      break;
    } else {
      // Skip unknown chunk (even-byte padded)
      uint32_t skip = cSize + (cSize & 1);
      uint8_t tmp[64];
      while (skip > 0) {
        int r = playSdFile.read(tmp, min((uint32_t)sizeof(tmp), skip));
        if (r <= 0) break;
        skip -= r;
      }
    }
  }

  if (!gotFmt || !gotData || dataBytes == 0) {
    playSdFile.close();
    return false;
  }

  sdFileSampleRate     = foundSR;
  sdFileChannels       = constrain((int)foundCh, 1, 2);
  sdFileDataBytesRem   = dataBytes;
  sdPlayTotalDataBytes = dataBytes;
  sdFileEof            = false;
  sdStreamWriteFrame   = 0;
  sdStreamReadFrame    = 0;
  sdFracOffset         = 0.0;
  playerIsActive       = true;
  g_playSource         = SRC_SD_WAV;

  logPrintf("[PLAY] %s | %u Hz | %d ch | Mode: %d\n", fullPath, foundSR, sdFileChannels, (int)g_playerMode);
  return true;
}

// Called smoothly from loop() on Core 0 to keep SPSC PSRAM ring buffer full
void pumpSDStreamFromCore0() {
  if (g_playSource != SRC_SD_WAV || sdFileEof) return;

  const uint32_t MAX_FRAMES = SD_STREAM_SAMPLES / 2;
  uint32_t readPos = sdStreamReadFrame;
  uint32_t writePos = sdStreamWriteFrame;
  uint32_t bufferedFrames = writePos - readPos;
  uint32_t freeFrames = MAX_FRAMES - bufferedFrames;

  // Refill in 2048-frame blocks whenever >= 2048 frames free
  if (freeFrames < 2048) return;

  static int16_t tempReadBuf[2048 * 2];
  uint32_t bytesPerFrame = sdFileChannels * sizeof(int16_t);
  uint32_t maxReadFrames = min((uint32_t)2048, (uint32_t)(sdFileDataBytesRem / bytesPerFrame));

  if (maxReadFrames == 0) {
    if (sdLoopPlayback) {
#if USE_SDFAT
      playSdFile.seekSet(sdDataStartOffset);
#else
      playSdFile.seek(sdDataStartOffset);
#endif
      sdFileDataBytesRem = sdPlayTotalDataBytes;
      maxReadFrames = min((uint32_t)2048, (uint32_t)(sdFileDataBytesRem / bytesPerFrame));
    }
    if (maxReadFrames == 0) {
      sdFileEof = true;
      return;
    }
  }

  uint32_t bytesToRead = maxReadFrames * bytesPerFrame;
  int bytesGot = playSdFile.read((uint8_t*)tempReadBuf, bytesToRead);
  if (bytesGot <= 0) {
    sdFileEof = true;
    return;
  }

  sdFileDataBytesRem -= bytesGot;
  uint32_t framesGot = bytesGot / bytesPerFrame;

  for (uint32_t i = 0; i < framesGot; i++) {
    uint32_t ringSlot = ((writePos + i) % MAX_FRAMES) * 2;
    if (sdFileChannels == 2) {
      sdStreamBuf[ringSlot]     = tempReadBuf[i * 2];
      sdStreamBuf[ringSlot + 1] = tempReadBuf[i * 2 + 1];
    } else {
      sdStreamBuf[ringSlot]     = tempReadBuf[i];
      sdStreamBuf[ringSlot + 1] = tempReadBuf[i];
    }
  }
  sdStreamWriteFrame = writePos + framesGot;

  if (sdFileDataBytesRem < bytesPerFrame) {
    if (sdLoopPlayback) {
#if USE_SDFAT
      playSdFile.seekSet(sdDataStartOffset);
#else
      playSdFile.seek(sdDataStartOffset);
#endif
      sdFileDataBytesRem = sdPlayTotalDataBytes;
    } else {
      sdFileEof = true;
    }
  }
}

void triggerInstantTEReplay() {
  if (ringBuffer == nullptr || teSnapshotBuf == nullptr || ringValidSamples < 4000) return;
  stopPlayback();

  isSavingWav = true;
  delay(10);

  uint32_t copySamps = min((uint32_t)ringValidSamples, (uint32_t)TE_SNAP_SAMPLES);
  uint32_t startIdx  = ((uint32_t)ringWriteIdx + (uint32_t)RING_SAMPLES - copySamps) % (uint32_t)RING_SAMPLES;
  uint32_t firstPart = min(copySamps, (uint32_t)(RING_SAMPLES - startIdx));

  memcpy(teSnapshotBuf, &ringBuffer[startIdx], firstPart * sizeof(int16_t));
  if (copySamps > firstPart) {
    memcpy(&teSnapshotBuf[firstPart], &ringBuffer[0], (copySamps - firstPart) * sizeof(int16_t));
  }

  teTotalFrames = copySamps / 2;
  teReadFramePos = 0.0;
  isSavingWav = false;

  g_playSource = SRC_TE_RING;
  logPrintf("[TE-LIVE] Replaying %.1fs ring buffer at %dx Time Expansion...\n",
            (float)teTotalFrames / (float)ADC_SAMPLE_RATE, g_teSpeedFactor);
}

// ------------------------------------------------------------------------------
// CORE 1 REAL-TIME AUDIO DSP & PLAYBACK TASK
// ------------------------------------------------------------------------------
void audioDspTask(void* param) {
  float prevRawL = 0.0f, hpL = 0.0f;
  float prevRawR = 0.0f, hpR = 0.0f;
  float prevHpL = 0.0f, prevHpR = 0.0f;
  float lpfOutL = 0.0f, lpfOutR = 0.0f;

  // Broadband Frequency Division (/10) state
  float fdPrevIn = 0.0f, fdHp15 = 0.0f;
  float fdEnv = 0.0f;
  int8_t fdSchmittState = -1;
  int fdEdgeCount = 0;
  float fdPolarity = 1.0f;

  uint32_t phaseAcc1 = 0, phaseAcc2 = 0, sdPhaseAcc = 0;
  uint32_t framesCounted = 0;
  unsigned long rateTimer = millis();

  while (true) {
    // ----------------------------------------------------------------------
    // BRANCH A: TIME-EXPANSION LIVE RING REPLAY OR SD WAV PLAYBACK
    // ----------------------------------------------------------------------
    if (g_playSource == SRC_TE_RING) {
      double step = ((double)ADC_SAMPLE_RATE / (double)g_teSpeedFactor) / (double)DAC_SAMPLE_RATE;
      float gain = (float)constrain((int)g_micGain, 1, 64);
      float vol  = getEffectiveVolume();
      int outCount = 0;

      uint32_t snapStart = (uint32_t)teReadFramePos;
      if (!fftCaptureReady && snapStart + FFT_SIZE < teTotalFrames) {
        for (int f = 0; f < FFT_SIZE; f++) {
          fftCaptureBuf[f] = ((float)teSnapshotBuf[(snapStart + f) * 2] + (float)teSnapshotBuf[(snapStart + f) * 2 + 1]) * 0.5f;
        }
        fftSampleRateHz = (float)ADC_SAMPLE_RATE;
        fftCaptureReady = true;
      }

      for (int i = 0; i < DAC_OUT_FRAMES; i++) {
        uint32_t idx0 = (uint32_t)teReadFramePos;
        if (idx0 + 1 >= teTotalFrames) {
          g_playSource = SRC_LIVE_ADC;
          break;
        }
        float frac = (float)(teReadFramePos - (double)idx0);
        float sL = (float)teSnapshotBuf[idx0 * 2] * (1.0f - frac) + (float)teSnapshotBuf[(idx0 + 1) * 2] * frac;
        float sR = (float)teSnapshotBuf[idx0 * 2 + 1] * (1.0f - frac) + (float)teSnapshotBuf[(idx0 + 1) * 2 + 1] * frac;

        float oL = constrain(sL * gain * vol, -32700.0f, 32700.0f);
        float oR = constrain(sR * gain * vol, -32700.0f, 32700.0f);
        dacDmaBuf[i * 2]     = (int16_t)oL;
        dacDmaBuf[i * 2 + 1] = (int16_t)oR;
        teReadFramePos += step;
        outCount++;
      }
      if (outCount > 0) {
        size_t w = 0;
        i2s_write(I2S_DAC_PORT, dacDmaBuf, outCount * 2 * sizeof(int16_t), &w, pdMS_TO_TICKS(20));
      }
      continue;
    }

    if (g_playSource == SRC_SD_WAV) {
      const uint32_t MAX_FRAMES = SD_STREAM_SAMPLES / 2;
      PlayerListenMode pMode = g_playerMode;
      float gain = (float)constrain((int)g_micGain, 1, 64);
      float vol  = getEffectiveVolume();

      uint32_t rFrame = sdStreamReadFrame;
      uint32_t wFrame = sdStreamWriteFrame;
      uint32_t availFrames = wFrame - rFrame;

      if (availFrames < 1024 && !sdFileEof) {
        memset(dacDmaBuf, 0, sizeof(dacDmaBuf));
        size_t w = 0;
        i2s_write(I2S_DAC_PORT, dacDmaBuf, sizeof(dacDmaBuf), &w, pdMS_TO_TICKS(10));
        continue;
      }
      if (availFrames < 8 && sdFileEof) {
        g_playSource = SRC_LIVE_ADC;
        playerIsActive = false;
        continue;
      }

      // Live Spectrogram FFT snapshot directly from the playing SD file!
      if (!fftCaptureReady && availFrames >= FFT_SIZE) {
        for (int f = 0; f < FFT_SIZE; f++) {
          uint32_t slot = ((rFrame + f) % MAX_FRAMES) * 2;
          fftCaptureBuf[f] = ((float)sdStreamBuf[slot] + (float)sdStreamBuf[slot + 1]) * 0.5f;
        }
        fftSampleRateHz = (float)sdFileSampleRate;
        fftCaptureReady = true;
      }

      if (pMode == PLAY_TE_10X || pMode == PLAY_TE_5X) {
        int teFactor = (pMode == PLAY_TE_10X) ? 10 : 5;
        double step = ((double)sdFileSampleRate / (double)teFactor) / (double)DAC_SAMPLE_RATE;
        double localPos = (double)rFrame + sdFracOffset;
        int outCount = 0;

        for (int i = 0; i < DAC_OUT_FRAMES; i++) {
          uint32_t idx0 = (uint32_t)localPos;
          if (idx0 + 1 >= wFrame) break;
          float frac = (float)(localPos - (double)idx0);

          uint32_t slot0 = (idx0 % MAX_FRAMES) * 2;
          uint32_t slot1 = ((idx0 + 1) % MAX_FRAMES) * 2;

          float sL = (float)sdStreamBuf[slot0]     * (1.0f - frac) + (float)sdStreamBuf[slot1]     * frac;
          float sR = (float)sdStreamBuf[slot0 + 1] * (1.0f - frac) + (float)sdStreamBuf[slot1 + 1] * frac;

          dacDmaBuf[i * 2]     = (int16_t)constrain(sL * gain * vol, -32700.0f, 32700.0f);
          dacDmaBuf[i * 2 + 1] = (int16_t)constrain(sR * gain * vol, -32700.0f, 32700.0f);

          localPos += step;
          outCount++;
        }

        uint32_t consumed = (uint32_t)localPos;
        sdFracOffset = localPos - (double)consumed;
        sdStreamReadFrame = consumed;

        if (outCount > 0) {
          size_t w = 0;
          i2s_write(I2S_DAC_PORT, dacDmaBuf, outCount * 2 * sizeof(int16_t), &w, pdMS_TO_TICKS(20));
        }
      } else {
        // Real-Time 1x Heterodyne (or DR if 0kHz) / Frequency Division on SD File!
        int oversample = max(1, (int)round((double)sdFileSampleRate / (double)DAC_SAMPLE_RATE));
        int fHet = g_freq1KHz;
        uint32_t sdStep = (fHet > 0) ? (uint32_t)(((double)fHet * 1000.0 / (double)sdFileSampleRate) * 4294967296.0) : 0;
        int outCount = 0;
        uint32_t pos = rFrame;

        for (int i = 0; i < DAC_OUT_FRAMES; i++) {
          if (pos + (uint32_t)oversample > wFrame) break;
          float sum = 0.0f;

          for (int k = 0; k < oversample; k++) {
            uint32_t slot = ((pos + k) % MAX_FRAMES) * 2;
            float mono = ((float)sdStreamBuf[slot] + (float)sdStreamBuf[slot + 1]) * 0.5f;

            if (pMode == PLAY_FD_1X) {
              fdHp15 = 0.65f * (fdHp15 + mono - fdPrevIn);
              fdPrevIn = mono;
              float mag = fabsf(fdHp15);
              if (mag > fdEnv) fdEnv = 0.25f * mag + 0.75f * fdEnv;
              else             fdEnv = 0.002f * mag + 0.998f * fdEnv;
              const float hyst = 80.0f;
              if (fdSchmittState < 0 && fdHp15 > hyst) {
                fdSchmittState = 1;
                if (++fdEdgeCount >= 5) { fdEdgeCount = 0; fdPolarity = -fdPolarity; }
              } else if (fdSchmittState > 0 && fdHp15 < -hyst) {
                fdSchmittState = -1;
              }
              sum += (fdEnv > hyst) ? (fdPolarity * fdEnv) : 0.0f;
            } else {
              if (fHet == 0) {
                sum += mono;
              } else {
                float lo = sinLUT[sdPhaseAcc >> 22];
                sdPhaseAcc += sdStep;
                sum += (mono * lo * 2.0f);
              }
            }
          }

          pos += oversample;
          float avg = sum / (float)oversample;
          lpfOutL = lpfOutL + 0.55f * (avg - lpfOutL);
          int16_t sOut = (int16_t)constrain(lpfOutL * gain * vol, -32700.0f, 32700.0f);
          dacDmaBuf[i * 2]     = sOut;
          dacDmaBuf[i * 2 + 1] = sOut;
          outCount++;
        }

        sdStreamReadFrame = pos;
        sdFracOffset = 0.0;

        if (outCount > 0) {
          size_t w = 0;
          i2s_write(I2S_DAC_PORT, dacDmaBuf, outCount * 2 * sizeof(int16_t), &w, pdMS_TO_TICKS(20));
        }
      }
      continue;
    }

    // ----------------------------------------------------------------------
    // BRANCH B: LIVE 192kHz ADC CAPTURE & REAL-TIME DSP
    // ----------------------------------------------------------------------
    if (isSavingWav) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    size_t bytesRead = 0;
    esp_err_t res = i2s_read(I2S_ADC_PORT, adcDmaBuf, sizeof(adcDmaBuf), &bytesRead, pdMS_TO_TICKS(80));

    if (res != ESP_OK || bytesRead == 0) {
      adcClockAlive = false;
      g_vuPeak = 0;
      g_ultraPeak = 0.0f;
      memset(dacDmaBuf, 0, sizeof(dacDmaBuf));
      size_t written = 0;
      i2s_write(I2S_DAC_PORT, dacDmaBuf, sizeof(dacDmaBuf), &written, pdMS_TO_TICKS(20));
      continue;
    }

    adcClockAlive = true;
    int totalFrames = bytesRead / (2 * sizeof(int32_t));
    framesCounted += totalFrames;
    if (millis() - rateTimer >= 1000) {
      measuredSampleRate = framesCounted;
      framesCounted = 0;
      rateTimer = millis();
    }

    int f1KHz = g_freq1KHz;
    int f2KHz = g_freq2KHz;
    uint32_t step1 = (f1KHz > 0) ? (uint32_t)(((double)f1KHz * 1000.0 / (double)ADC_SAMPLE_RATE) * 4294967296.0) : 0;
    uint32_t step2 = (f2KHz > 0) ? (uint32_t)(((double)f2KHz * 1000.0 / (double)ADC_SAMPLE_RATE) * 4294967296.0) : 0;

    float gain = (float)g_micGain;
    float vol = getEffectiveVolume();
    bool mix = g_mixMode;
    InputChannelMode chMode = g_inputCh;
    ListenMode lMode = g_listenMode;

    float blockMaxPeak = 0.0f;
    float blockUltraMax = 0.0f;
    int outIdx = 0;

    // Grab a 256-sample FFT snapshot if Core 0 is ready
    bool captureFFT = (!fftCaptureReady && totalFrames >= FFT_SIZE);

    for (int f = 0; f + 3 < totalFrames; f += 4) {
      float sumL = 0.0f;
      float sumR = 0.0f;

      for (int k = 0; k < 4; k++) {
        int idx = f + k;
        int32_t s32L = adcDmaBuf[idx * 2];
        int32_t s32R = adcDmaBuf[idx * 2 + 1];

        // Channel Selection (avoids dead channel noise if only 1 mic is connected!)
        if (chMode == IN_L_ONLY)      s32R = s32L;
        else if (chMode == IN_R_ONLY) s32L = s32R;

        float rawL = (float)(s32L >> 8);
        float rawR = (float)(s32R >> 8);

        if (ringBuffer != nullptr) {
          ringBuffer[ringWriteIdx]     = (int16_t)(s32L >> 16);
          ringBuffer[ringWriteIdx + 1] = (int16_t)(s32R >> 16);
          ringWriteIdx = (ringWriteIdx + 2) % RING_SAMPLES;
          if (ringValidSamples < RING_SAMPLES) ringValidSamples += 2;
        }

        // DC-blocker / Wind Rumble Rejection
        hpL = rawL - prevRawL + 0.995f * hpL;
        prevRawL = rawL;
        hpR = rawR - prevRawR + 0.995f * hpR;
        prevRawR = rawR;

        if (captureFFT && idx < FFT_SIZE) {
          fftCaptureBuf[idx] = ((hpL + hpR) * 0.5f) / 256.0f;
        }

        float ultraL = fabsf(hpL - prevHpL);
        float ultraR = fabsf(hpR - prevHpR);
        prevHpL = hpL;
        prevHpR = hpR;
        float uMax = (ultraL > ultraR) ? ultraL : ultraR;
        if (uMax > blockUltraMax) blockUltraMax = uMax;

        float aMax = (fabsf(hpL) > fabsf(hpR)) ? fabsf(hpL) : fabsf(hpR);
        if (aMax > blockMaxPeak) blockMaxPeak = aMax;

        if (lMode == LISTEN_FD) {
          // Broadband Frequency Division (/10) across 15 - 96 kHz
          float mono = (hpL + hpR) * 0.5f;
          fdHp15 = 0.65f * (fdHp15 + mono - fdPrevIn);
          fdPrevIn = mono;

          float mag = fabsf(fdHp15);
          if (mag > fdEnv) fdEnv = 0.25f * mag + 0.75f * fdEnv;
          else             fdEnv = 0.002f * mag + 0.998f * fdEnv;

          const float hyst = 300.0f;
          if (fdSchmittState < 0 && fdHp15 > hyst) {
            fdSchmittState = 1;
            fdEdgeCount++;
            if (fdEdgeCount >= 5) { // 5 rising edges toggle = /10 frequency divide
              fdEdgeCount = 0;
              fdPolarity = -fdPolarity;
            }
          } else if (fdSchmittState > 0 && fdHp15 < -hyst) {
            fdSchmittState = -1;
          }

          float fdOut = (fdEnv > hyst) ? (fdPolarity * fdEnv) : 0.0f;
          sumL += fdOut;
          sumR += fdOut;
        } else {
          // Dual-Channel Stereo Heterodyne
          if (f1KHz == 0) {
            sumL += hpL;
          } else {
            float lo1 = sinLUT[phaseAcc1 >> 22];
            phaseAcc1 += step1;
            sumL += (hpL * lo1 * 2.0f);
          }

          if (f2KHz == 0) {
            sumR += hpR;
          } else {
            float lo2 = sinLUT[phaseAcc2 >> 22];
            phaseAcc2 += step2;
            sumR += (hpR * lo2 * 2.0f);
          }
        }
      }

      lpfOutL = lpfOutL + 0.55f * ((sumL * 0.25f) - lpfOutL);
      lpfOutR = lpfOutR + 0.55f * ((sumR * 0.25f) - lpfOutR);

      float outL, outR;
      if (mix || lMode == LISTEN_FD) {
        float mixed = 0.5f * (lpfOutL + lpfOutR);
        outL = outR = (mixed / 256.0f) * gain * vol;
      } else {
        outL = (lpfOutL / 256.0f) * gain * vol;
        outR = (lpfOutR / 256.0f) * gain * vol;
      }

      if (outIdx < DAC_OUT_FRAMES) {
        dacDmaBuf[outIdx * 2]     = (int16_t)constrain(outL, -32700.0f, 32700.0f);
        dacDmaBuf[outIdx * 2 + 1] = (int16_t)constrain(outR, -32700.0f, 32700.0f);
        outIdx++;
      }
    }

    if (captureFFT) {
      fftSampleRateHz = (float)ADC_SAMPLE_RATE;
      fftCaptureReady = true;
    }

    if (outIdx > 0) {
      size_t written = 0;
      i2s_write(I2S_DAC_PORT, dacDmaBuf, outIdx * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(10));
    }

    int vu = 0;
    if (blockMaxPeak > 3000.0f) {
      vu = constrain((int)(log10f(blockMaxPeak / 3000.0f) * 35.0f), 1, 100);
    }
    g_vuPeak = vu;
    g_ultraPeak = blockUltraMax;
  }
}

// ------------------------------------------------------------------------------
// OLED DISPLAY RENDERER (ALL 4 PAGES!)
// ------------------------------------------------------------------------------
void wakeOLED() {
  lastUserActivityMs = millis();
  if (oledSleeping && oledPresent) {
    oledSleeping = false;
    display.ssd1306_command(SSD1306_DISPLAYON);
  }
}

static void drawLabelBadge(int x, int w, const char* txt, bool active) {
  if (active) {
    display.fillRect(x, 0, w, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(x + 2, 1);
  display.print(txt);
}

static void drawSpectrumBars(int baseLineY, int maxBarHeight, bool showHetCursor) {
  display.drawLine(0, baseLineY + 1, 127, baseLineY + 1, SSD1306_WHITE);

  for (int x = 0; x < 128; x++) {
    int bin = map(x, 0, 127, fftEngine.minBin, fftEngine.maxBin);
    float mag = fftEngine.spectrumBins[bin];
    int h = 0;
    if (mag > 2.5f) {
      h = constrain((int)(log10f(mag / 2.0f) * ((float)maxBarHeight / 2.6f)), 1, maxBarHeight);
    }
    if (h > 0) {
      display.drawFastVLine(x, baseLineY - h, h, SSD1306_WHITE);
    }
  }

  if (showHetCursor && (float)g_freq1KHz >= fftEngine.dispMinKHz && (float)g_freq1KHz <= fftEngine.dispMaxKHz) {
    int markerX = fftEngine.freqToX((float)g_freq1KHz, 128);
    int topY = max(8, baseLineY - maxBarHeight - 1);
    display.drawTriangle(markerX - 2, topY, markerX + 2, topY, markerX, topY + 3, SSD1306_WHITE);
  }
}

static const char* getPlayerModeTag(PlayerListenMode m) {
  switch (m) {
    case PLAY_HET_1X: return "HET 1x";
    case PLAY_FD_1X:  return "FD /10";
    case PLAY_TE_10X: return "TE 10x";
    case PLAY_TE_5X:  return "TE  5x";
  }
  return "HET 1x";
}

// ------------------------------------------------------------------------------
// LIVE BATTERY TELEMETRY & SLEEK COMPACT ICON RENDERER
// ------------------------------------------------------------------------------
int calculateBatteryPercent(float volts) {
  if (volts >= 4.18f) return 100;
  if (volts <= 3.25f) return 0;
  if (volts > 3.95f) return 80 + (int)((volts - 3.95f) / (4.18f - 3.95f) * 20.0f);
  if (volts > 3.75f) return 40 + (int)((volts - 3.75f) / (3.95f - 3.75f) * 40.0f);
  if (volts > 3.55f) return 15 + (int)((volts - 3.55f) / (3.75f - 3.55f) * 25.0f);
  return (int)((volts - 3.25f) / (3.55f - 3.25f) * 15.0f);
}

void updateBatteryReading() {
  unsigned long now = millis();
  if (now - g_lastBatCheckMs < 2000 && g_lastBatCheckMs != 0) return;
  g_lastBatCheckMs = now;

  uint32_t mv = analogReadMilliVolts(PIN_BATTERY_ADC);
  // If pin is floating (< 0.25V, no divider wired yet), show nominal 3.95V (80%) so UI looks great
  if (mv < 250) {
    g_batVolts = 3.95f;
    g_batPercent = 80;
    return;
  }

  float rawV = ((float)mv / 1000.0f) * BAT_DIVIDER_RATIO;
  if (g_batVolts <= 0.5f) g_batVolts = rawV;
  else g_batVolts = 0.85f * g_batVolts + 0.15f * rawV;

  g_batPercent = constrain(calculateBatteryPercent(g_batVolts), 0, 100);
}

void drawBatteryIcon(int x, int y, int pct) {
  const int bw = 15;
  const int bh = 7;

  // Flash icon outline when battery is critical (<15%)
  if (pct <= 15 && (millis() / 500) % 2 == 0) return;

  // Battery body outline
  display.drawRect(x, y, bw, bh, SSD1306_WHITE);
  // Terminal bump on the right
  display.drawFastVLine(x + bw, y + 2, 3, SSD1306_WHITE);

  // Proportional inner fill bar
  int innerW = bw - 4; // 11 px
  int fillW = map(constrain(pct, 0, 100), 0, 100, 0, innerW);
  if (fillW > 0) {
    display.fillRect(x + 2, y + 2, fillW, 3, SSD1306_WHITE);
  }
}

void drawUI(const char* flashBanner = nullptr) {
  if (!oledPresent || oledSleeping) return;

  updateBatteryReading();

  display.clearDisplay();
  display.setTextSize(1);

  if (flashBanner != nullptr) {
    display.setFont(&FreeSerif9pt7b);
    display.setTextColor(SSD1306_WHITE);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(flashBanner, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(max(0, (OLED_WIDTH - (int)w) / 2), 21);
    display.print(flashBanner);
    display.display();
    return;
  }

  // If TE Live Replay is active, show live progress bar banner
  if (g_playSource == SRC_TE_RING) {
    display.setFont();
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 2);
    display.printf("TE-LIVE REPLAY (%dx)", g_teSpeedFactor);
    display.drawRect(8, 16, 112, 10, SSD1306_WHITE);
    int pw = (teTotalFrames > 0) ? map((int)(teReadFramePos), 0, (int)teTotalFrames, 0, 110) : 0;
    if (pw > 0) display.fillRect(9, 17, constrain(pw, 0, 110), 8, SSD1306_WHITE);
    display.display();
    return;
  }

  // ----------------------------------------------------------------------
  // PAGE 1: MAIN DETECTOR UI
  // ----------------------------------------------------------------------
  if (currentPage == PAGE_MAIN) {
    display.setFont();
    if (g_listenMode == LISTEN_FD) {
      drawLabelBadge(0,  25, "FD",  false);
    } else {
      drawLabelBadge(0,  25, "F1k", activeField == FIELD_F1);
    }
    drawLabelBadge(32,  25, "VOL", activeField == FIELD_VOL);
    drawLabelBadge(66,  25, "GNx", activeField == FIELD_GAIN);
    drawLabelBadge(102, 25, (g_listenMode == LISTEN_FD) ? "PKk" : "F2k",
                   (g_listenMode == LISTEN_HET && activeField == FIELD_F2));

    display.setFont(&FreeSerif9pt7b);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 22);
    if (g_listenMode == LISTEN_FD) display.print("/10");
    else if (g_freq1KHz == 0)     display.print("DR");
    else                           display.print(g_freq1KHz);

    display.setCursor(32, 22);
    display.print(g_volume);

    display.setCursor(66, 22);
    display.print(g_micGain);

    display.setCursor(102, 22);
    if (g_listenMode == LISTEN_FD) {
      if (g_peakFreqKHz > 0.0f) display.print((int)roundf(g_peakFreqKHz));
      else display.print("--");
    } else {
      if (g_freq2KHz == 0) display.print("DR");
      else display.print(g_freq2KHz);
    }

    // Bottom status bar
    display.setFont();
    display.setTextSize(1);
    display.setCursor(0, 25);
    if (g_inputCh == IN_L_ONLY)      display.print("L1");
    else if (g_inputCh == IN_R_ONLY) display.print("R1");
    else                             display.print(g_mixMode ? "MX" : "ST");

    display.setCursor(16, 25);
    display.printf("#%02d", nextFileIndex % 100);

    if (!adcClockAlive) {
      display.setCursor(38, 25);
      display.print("NO-ADC");
    } else {
      display.drawRect(37, 26, 36, 5, SSD1306_WHITE);
      int fillW = map(g_vuPeak, 0, 100, 0, 34);
      if (fillW > 0) display.fillRect(38, 27, fillW, 3, SSD1306_WHITE);
    }

    if (!sdMounted) {
      display.setCursor(76, 25);
      display.print("NOSD");
    } else if (wifiActive) {
      display.fillRect(75, 24, 23, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      display.setCursor(76, 25);
      display.print("WIFI");
    } else if (autoModeEnabled) {
      display.fillRect(75, 24, 23, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      display.setCursor(76, 25);
      display.print(autoTriggerPending ? "TRG" : "AUT");
    } else {
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(76, 25);
      display.print("MAN");
    }

    drawBatteryIcon(108, 25, g_batPercent);
  }
  // ----------------------------------------------------------------------
  // PAGE 2: LIVE ULTRASONIC SPECTRUM ANALYZER
  // ----------------------------------------------------------------------
  else if (currentPage == PAGE_SPECTRUM) {
    display.setFont();
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    if (g_peakFreqKHz >= fftEngine.dispMinKHz) {
      display.printf("PK:%4.1fk", g_peakFreqKHz);
    } else {
      display.print("PK: --.-k");
    }

    bool isFD  = (g_playSource == SRC_SD_WAV) ? (g_playerMode == PLAY_FD_1X)  : (g_listenMode == LISTEN_FD);
    bool isHET = (g_playSource == SRC_SD_WAV) ? (g_playerMode == PLAY_HET_1X) : (g_listenMode == LISTEN_HET);

    display.setCursor(52, 0);
    if (g_playSource == SRC_SD_WAV && !isHET && !isFD) {
      display.printf("[%s]", getPlayerModeTag(g_playerMode));
    } else if (isFD) {
      display.print("[FD /10]");
    } else if (g_freq1KHz == 0) {
      display.print("[DIRECT]");
    } else {
      display.printf("HET:%2dk", g_freq1KHz);
    }

    drawBatteryIcon(110, 1, g_batPercent);

    drawSpectrumBars(30, 19, isHET);
  }
  // ----------------------------------------------------------------------
  // PAGE 3: MICRO-SD WAV PLAYER (LIVE SPECTROGRAM + HET 1x / FD / TE 10x)
  // ----------------------------------------------------------------------
  else if (currentPage == PAGE_PLAYER) {
    display.setFont();
    display.setTextColor(SSD1306_WHITE);

    if (g_playSource == SRC_SD_WAV) {
      // Live Playback Spectrogram View!
      display.setCursor(0, 0);
      if (g_playerMode == PLAY_HET_1X) {
        if (g_freq1KHz == 0) display.print("DIRECT ");
        else                 display.printf("HET:%2dk", g_freq1KHz);
      } else {
        display.print(getPlayerModeTag(g_playerMode));
      }

      display.setCursor(44, 0);
      if (g_peakFreqKHz >= fftEngine.dispMinKHz) {
        display.printf("PK:%4.1fk", g_peakFreqKHz);
      } else {
        display.print("PK:--.-k");
      }

      int pct = (sdPlayTotalDataBytes > 0)
                  ? constrain((int)(100ULL * (sdPlayTotalDataBytes - sdFileDataBytesRem) / sdPlayTotalDataBytes), 0, 100)
                  : 0;
      display.setCursor(80, 0);
      display.printf("%2d%%", pct);

      drawBatteryIcon(110, 1, g_batPercent);

      drawSpectrumBars(30, 19, (g_playerMode == PLAY_HET_1X));
    } else {
      // File Browser View
      display.setCursor(0, 0);
      display.printf("SD [%s]", getPlayerModeTag(g_playerMode));
      display.setCursor(68, 0);
      display.printf("%d/%d", wavListCount > 0 ? (playerSelectedIdx + 1) : 0, wavListCount);

      drawBatteryIcon(110, 1, g_batPercent);

      display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

      if (!sdMounted) {
        display.setCursor(14, 16);
        display.print("NO MICRO-SD CARD");
      } else if (wavListCount == 0) {
        display.setCursor(12, 16);
        display.print("NO .WAV FILES FOUND");
      } else {
        display.setCursor(0, 13);
        display.print("> ");
        char dispName[20];
        const char* fullNm = wavList[playerSelectedIdx].name;
        size_t nlen = strlen(fullNm);
        if (nlen <= 18) {
          snprintf(dispName, sizeof(dispName), "%s", fullNm);
        } else {
          // Truncate long names cleanly while keeping extension (e.g. "real_bat_43..wav")
          snprintf(dispName, sizeof(dispName), "%.12s..%s", fullNm, fullNm + nlen - 4);
        }
        display.print(dispName);

        display.setCursor(0, 23);
        float mb = (float)wavList[playerSelectedIdx].sizeBytes / (1024.0f * 1024.0f);
        display.printf("%.2fMB |1x:Ply H:Mod", mb);
      }
    }
  }
  // ----------------------------------------------------------------------
  // PAGE 4: FIELD SETTINGS & WIFI CONTROL MENU
  // ----------------------------------------------------------------------
  else if (currentPage == PAGE_SETTINGS) {
    display.setFont();
    display.setTextColor(SSD1306_WHITE);

    // Show 3 lines around settingsCursor
    int topIdx = constrain(settingsCursor - 1, 0, SETTINGS_COUNT - 3);

    for (int row = 0; row < 3; row++) {
      int item = topIdx + row;
      int y = row * 11;
      bool sel = (item == settingsCursor);

      if (sel) {
        display.fillRect(0, y, 128, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }

      display.setCursor(2, y + 1);
      switch (item) {
        case 0:
          display.printf("Mode  : %s", g_listenMode == LISTEN_HET ? "HETERODYNE" : "FREQ DIV /10");
          break;
        case 1: {
          const char* chLbl = "MONO MIX (MX)";
          if (g_inputCh == IN_L_ONLY)      chLbl = "LEFT MIC ONLY";
          else if (g_inputCh == IN_R_ONLY) chLbl = "RIGHT MIC ONLY";
          else if (!g_mixMode)             chLbl = "TRUE STEREO";
          display.printf("Route : %s", chLbl);
          break;
        }
        case 2:
          display.printf("VolCrv: %s", g_volCurve == VOL_CURVE_LOG ? "LOG (EARS)" : "LINEAR");
          break;
        case 3:
          display.printf("AutoPM: %s", autoModeEnabled ? "ON (ARMED)" : "OFF (MANUAL)");
          break;
        case 4:
          display.printf("Thresh: %s%d%s", settingsEditingThresh ? "[" : "", (int)g_trigThreshold, settingsEditingThresh ? "]" : "");
          break;
        case 5:
          display.printf("TE 2s : PLAY (%dx)", g_teSpeedFactor);
          break;
        case 6:
          display.printf("WiFiAP: %s", wifiActive ? "192.168.4.1" : "OFF");
          break;
        case 7:
          if (sdMounted) display.printf("SDCard: %uMB (%d)", sdTotalMB, wavListCount);
          else display.print("SDCard: RESCAN");
          break;
        case 8:
          display.printf("Bat   : %.2fV (%d%%)", g_batVolts, g_batPercent);
          break;
        case 9:
          display.print("ADC Test SigGen >");
          break;
        case 10:
          display.print("< Exit to Main");
          break;
      }
    }
  }
  // ----------------------------------------------------------------------
  // PAGE 5: ADC DIAGNOSTIC & SIGNAL GENERATOR (35-100 kHz)
  // ----------------------------------------------------------------------
  else if (currentPage == PAGE_TEST) {
    display.setFont();
    display.setTextSize(1);

    // Line 1 (y = 0..7): Parameters with badges for the active field
    char genStr[10];
    snprintf(genStr, sizeof(genStr), "G:%2dk", g_testGenFreqKHz);
    drawLabelBadge(0, 36, genStr, g_testActiveField == TEST_FIELD_GEN);

    char hetStr[10];
    if (g_freq1KHz == 0) snprintf(hetStr, sizeof(hetStr), "H:DR ");
    else                 snprintf(hetStr, sizeof(hetStr), "H:%2dk", g_freq1KHz);
    drawLabelBadge(38, 36, hetStr, g_testActiveField == TEST_FIELD_HET);

    const char* sigTag = (g_testGenState == TEST_SIG_ON) ? "ON " : (g_testGenState == TEST_SIG_SWEEP ? "SWP" : "OFF");
    drawLabelBadge(76, 26, sigTag, g_testActiveField == TEST_FIELD_STATE);

    char volStr[8];
    snprintf(volStr, sizeof(volStr), "V%02d", g_volume);
    drawLabelBadge(104, 24, volStr, g_testActiveField == TEST_FIELD_VOL);

    // Line 2 (y = 9..17): Peak Frequency and Beat / Difference Tone
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 11);
    if (g_peakFreqKHz >= fftEngine.dispMinKHz) {
      display.printf("PK:%4.1fk", g_peakFreqKHz);
    } else {
      display.print("PK: --.-k");
    }

    display.setCursor(48, 11);
    if (g_testGenState != TEST_SIG_OFF && g_freq1KHz > 0) {
      float diffKHz = fabsf((float)g_testGenFreqKHz - (float)g_freq1KHz);
      display.printf("B:%3.1fk", diffKHz);
    } else if (g_freq1KHz == 0) {
      display.print("DIRECT");
    } else {
      display.print("SIG OFF  ");
    }

    drawBatteryIcon(110, 10, g_batPercent);

    // Line 3 (y = 18..31): Live 128-column Spectrum Bar Graph + HET cursor
    drawSpectrumBars(30, 11, true);
  }

  display.display();
}

// ------------------------------------------------------------------------------
// SAVE 5-SECOND PSRAM RING BUFFER TO MICRO-SD (.WAV)
// ------------------------------------------------------------------------------
void saveRingBufferToSD(bool triggeredByAuto) {
  wakeOLED();
  stopPlayback();

  if (!sdMounted) {
    if (!initSDCard()) {
      drawUI("NO SD CARD!");
      delay(800);
      drawUI();
      return;
    }
  }
  if (ringBuffer == nullptr || ringValidSamples == 0) {
    drawUI("BUF EMPTY!");
    delay(600);
    drawUI();
    return;
  }

  isSavingWav = true;
  delay(15);

  char fname[24];
  snprintf(fname, sizeof(fname), "/REC%03d.WAV", nextFileIndex);

  // Safeguard: Advance nextFileIndex if file already exists on SD
#if USE_SDFAT
  while (sd.exists(fname)) {
#else
  while (SD.exists(fname)) {
#endif
    nextFileIndex++;
    snprintf(fname, sizeof(fname), "/REC%03d.WAV", nextFileIndex);
  }

  drawUI(triggeredByAuto ? "AUTO SAVING" : "Saving WAV...");
  logPrintf("[SAVE] Writing %s (%u frames @ 192kHz)...\n", fname, ringValidSamples / 2);

  uint32_t samplesToWrite = ringValidSamples;
  uint32_t dataBytes = samplesToWrite * sizeof(int16_t);
  uint32_t startIdx = (samplesToWrite < RING_SAMPLES) ? 0 : ringWriteIdx;

  WavHeader hdr;
  hdr.dataSize = dataBytes;
  hdr.chunkSize = dataBytes + sizeof(WavHeader) - 8;

  unsigned long t0 = millis();
  bool ok = false;

  uint32_t firstPart = min(samplesToWrite, (uint32_t)(RING_SAMPLES - startIdx));
  uint32_t remaining = samplesToWrite - firstPart;

#if USE_SDFAT
  if (wavFile.open(fname, O_WRONLY | O_CREAT | O_TRUNC)) {
    SdFileHelper::writeRingChunks(wavFile, hdr, startIdx, firstPart, remaining);
    wavFile.sync();
    wavFile.close();
    ok = true;
  }
#else
  wavFile = SD.open(fname, FILE_WRITE);
  if (wavFile) {
    SdFileHelper::writeRingChunks(wavFile, hdr, startIdx, firstPart, remaining);
    wavFile.close();
    ok = true;
  }
#endif

  if (ok) {
    logPrintf("[PASS] Saved %s in %lu ms!\n", fname, millis() - t0);
    scanSDWavFiles();
    drawUI("Saved!");
    delay(350);
  } else {
    logPrint("[FAIL] SD Write Failed!\n");
    drawUI("SD ERR!");
    delay(700);
  }

  isSavingWav = false;
  drawUI();
}

// ------------------------------------------------------------------------------
// SETUP
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial0.begin(115200);
  delay(1000);

  logPrint("\n=======================================================\n");
  logPrint("  ESPERDYNE BAT DETECTOR & PAM NODE - PRO EDITION v2.0 \n");
  logPrint("=======================================================\n");

  // 1. Initialize OLED FIRST (Exact proven bus scan sequence from test_02_oled)
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  byte i2cCount = 0;
  for (byte i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0) i2cCount++;
  }

  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))      oledPresent = true;
  else if (display.begin(SSD1306_SWITCHCAPVCC, 0x3D))      oledPresent = true;

  if (oledPresent) {
    display.setRotation(OLED_ROTATION);
    display.clearDisplay();
    display.setFont(&FreeSerif9pt7b);
    display.setTextColor(SSD1306_WHITE);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds("ESPERDYNE", 0, 0, &x1, &y1, &w, &h);
    display.setCursor((OLED_WIDTH - w) / 2, 16);
    display.print("ESPERDYNE");
    display.setFont();
    display.setTextSize(1);
    display.setCursor(16, 23);
    display.print("PRO EDITION v2.0");
    display.display();
    logPrint("[PASS] OLED Splash Screen Displayed.\n");
    delay(1200);
  } else {
    logPrintf("[FAIL] OLED init failed! I2C devices: %d\n", i2cCount);
  }

  // 2. Precompute LUT & FFT Windows
  for (int i = 0; i < 1024; i++) {
    sinLUT[i] = sinf((2.0f * M_PI * (float)i) / 1024.0f);
  }
  fftEngine.init();

  // 3. Allocate PSRAM Ring Buffer, TE Snapshot Buffer & SD Stream Buffer
  if (psramInit()) {
    ringBuffer    = (int16_t*)heap_caps_malloc((size_t)RING_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    teSnapshotBuf = (int16_t*)heap_caps_malloc((size_t)TE_SNAP_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    sdStreamBuf   = (int16_t*)heap_caps_malloc((size_t)SD_STREAM_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);

    if (ringBuffer)    memset(ringBuffer, 0, (size_t)RING_SAMPLES * sizeof(int16_t));
    if (teSnapshotBuf) memset(teSnapshotBuf, 0, (size_t)TE_SNAP_SAMPLES * sizeof(int16_t));
    if (sdStreamBuf)   memset(sdStreamBuf, 0, (size_t)SD_STREAM_SAMPLES * sizeof(int16_t));

    logPrintf("[PASS] PSRAM Allocated | Free PSRAM Remaining: %.2f MB\n", (float)ESP.getFreePsram() / (1024.0f * 1024.0f));
  } else {
    logPrint("[WARN] PSRAM not detected!\n");
  }

  // 4. Configure Inputs, Battery ADC & Diagnostic Signal Generator
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  pinMode(PIN_SIG_GEN, OUTPUT);
  digitalWrite(PIN_SIG_GEN, LOW);
  lastEncState = (digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT);

  pinMode(PIN_BATTERY_ADC, INPUT);
  analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
  updateBatteryReading();

  // 5. Initialize MicroSD Card
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  initSDCard();

  // 6. Start Dual I2S Audio Hardware + Core 1 Real-Time DSP Task
  setupI2S_DAC();
  setupI2S_ADC();

  xTaskCreatePinnedToCore(
    audioDspTask,
    "EsperdyneDSP",
    8192,
    NULL,
    configMAX_PRIORITIES - 2,
    NULL,
    1
  );

  lastUserActivityMs = millis();
  drawUI();

  logPrint("=======================================================\n");
  logPrint("PRO EDITION READY!\n");
  logPrint(" * 2x Click Knob : Switch Pages [MAIN->SPEC->PLAY->SET->TEST]\n");
  logPrint(" * PAGE 5 (TEST) : Loop GPIO 18 to RCA to test 35-100kHz ADC!\n");
  logPrint(" * 1x Click Knob : Select / Play / Toggle\n");
  logPrint(" * Hold Knob     : Save 5s WAV (or Speed 10x/5x/1x in Player)\n");
  logPrint(" * BOOT Tap/Hold : Instant Save / Toggle AUTO-PAM\n");
  logPrint("=======================================================\n\n");
}

// ------------------------------------------------------------------------------
// INPUT HANDLERS (PER PAGE)
// ------------------------------------------------------------------------------
void handleEncoderTurn(int step) {
  wakeOLED();

  auto stepFreq = [&](volatile int& freq) {
    if (freq == 0 && step > 0) freq = 10;
    else if (freq <= 10 && step < 0) freq = 0;
    else freq = constrain(freq + step * 2, 0, 90);
  };

  if (currentPage == PAGE_MAIN) {
    if (activeField == FIELD_F1) {
      stepFreq(g_freq1KHz);
      if (g_mixMode) g_freq2KHz = g_freq1KHz;
    } else if (activeField == FIELD_VOL) {
      g_volume = constrain(g_volume + step * 5, 0, 100);
    } else if (activeField == FIELD_GAIN) {
      if (step > 0 && g_micGain < 512) g_micGain *= 2;
      if (step < 0 && g_micGain > 1)   g_micGain /= 2;
    } else if (activeField == FIELD_F2) {
      stepFreq(g_freq2KHz);
    }
  }
  else if (currentPage == PAGE_SPECTRUM) {
    if (g_freq1KHz == 0 && step > 0) g_freq1KHz = 10;
    else if (g_freq1KHz <= 10 && step < 0) g_freq1KHz = 0;
    else g_freq1KHz = constrain(g_freq1KHz + step, 0, 90);
    if (g_mixMode) g_freq2KHz = g_freq1KHz;
  }
  else if (currentPage == PAGE_PLAYER) {
    if (wavListCount > 0 && g_playSource != SRC_SD_WAV) {
      playerSelectedIdx = (playerSelectedIdx + step + wavListCount) % wavListCount;
    } else if (g_playSource == SRC_SD_WAV) {
      if (g_playerMode == PLAY_HET_1X) {
        if (g_freq1KHz == 0 && step > 0) g_freq1KHz = 10;
        else if (g_freq1KHz <= 10 && step < 0) g_freq1KHz = 0;
        else g_freq1KHz = constrain(g_freq1KHz + step, 0, 90);
        if (g_mixMode) g_freq2KHz = g_freq1KHz;
      } else {
        g_volume = constrain(g_volume + step * 5, 0, 100);
      }
    }
  }
  else if (currentPage == PAGE_SETTINGS) {
    if (settingsEditingThresh) {
      g_trigThreshold = constrain(g_trigThreshold + step * 500.0f, 1000.0f, 25000.0f);
    } else {
      settingsCursor = (settingsCursor + step + SETTINGS_COUNT) % SETTINGS_COUNT;
    }
  }
  else if (currentPage == PAGE_TEST) {
    if (g_testActiveField == TEST_FIELD_GEN) {
      g_testGenFreqKHz = constrain(g_testGenFreqKHz + step, 35, 100);
      if (g_testGenState != TEST_SIG_OFF) {
        setSignalGenOutput(true, g_testGenFreqKHz);
      }
    } else if (g_testActiveField == TEST_FIELD_HET) {
      if (g_freq1KHz == 0 && step > 0) g_freq1KHz = 10;
      else if (g_freq1KHz <= 10 && step < 0) g_freq1KHz = 0;
      else g_freq1KHz = constrain(g_freq1KHz + step, 0, 90);
      if (g_mixMode) g_freq2KHz = g_freq1KHz;
    } else if (g_testActiveField == TEST_FIELD_STATE) {
      int s = (int)g_testGenState + step;
      if (s < 0) s = 2;
      if (s > 2) s = 0;
      g_testGenState = (TestGenState)s;
      setSignalGenOutput(g_testGenState != TEST_SIG_OFF, g_testGenFreqKHz);
    } else if (g_testActiveField == TEST_FIELD_VOL) {
      g_volume = constrain(g_volume + step * 5, 0, 100);
    }
  }
}

void handleSingleClick() {
  wakeOLED();

  if (currentPage == PAGE_MAIN) {
    if (g_listenMode == LISTEN_FD) {
      activeField = (activeField == FIELD_VOL) ? FIELD_GAIN : FIELD_VOL;
    } else if (g_mixMode) {
      if (activeField == FIELD_F1) activeField = FIELD_VOL;
      else if (activeField == FIELD_VOL) activeField = FIELD_GAIN;
      else activeField = FIELD_F1;
    } else {
      activeField = (EditField)((activeField + 1) % 4);
    }
  }
  else if (currentPage == PAGE_SPECTRUM) {
    if (g_playSource == SRC_SD_WAV) {
      g_playerMode = (g_playerMode == PLAY_HET_1X) ? PLAY_FD_1X : PLAY_HET_1X;
    } else {
      g_listenMode = (g_listenMode == LISTEN_HET) ? LISTEN_FD : LISTEN_HET;
    }
  }
  else if (currentPage == PAGE_PLAYER) {
    if (g_playSource == SRC_SD_WAV) {
      stopPlayback();
    } else if (sdMounted && wavListCount > 0) {
      startSDWavPlayback(wavList[playerSelectedIdx].name);
    }
  }
  else if (currentPage == PAGE_SETTINGS) {
    switch (settingsCursor) {
      case 0:
        g_listenMode = (g_listenMode == LISTEN_HET) ? LISTEN_FD : LISTEN_HET;
        if (g_listenMode == LISTEN_FD) activeField = FIELD_VOL;
        else activeField = FIELD_F1;
        break;
      case 1:
        if (g_inputCh == IN_LR_STEREO && g_mixMode) {
          g_mixMode = false; // MX -> ST
        } else if (g_inputCh == IN_LR_STEREO && !g_mixMode) {
          g_inputCh = IN_L_ONLY; g_mixMode = true; // ST -> LEFT ONLY
        } else if (g_inputCh == IN_L_ONLY) {
          g_inputCh = IN_R_ONLY; g_mixMode = true; // LEFT -> RIGHT ONLY
        } else {
          g_inputCh = IN_LR_STEREO; g_mixMode = true; // RIGHT -> MX
        }
        break;
      case 2:
        g_volCurve = (g_volCurve == VOL_CURVE_LOG) ? VOL_CURVE_LIN : VOL_CURVE_LOG;
        break;
      case 3:
        autoModeEnabled = !autoModeEnabled;
        autoTriggerPending = false;
        break;
      case 4:
        settingsEditingThresh = !settingsEditingThresh;
        break;
      case 5:
        currentPage = PAGE_MAIN;
        triggerInstantTEReplay();
        break;
      case 6:
        if (wifiActive) stopWifiCompanion();
        else startWifiCompanion();
        break;
      case 7:
        initSDCard();
        break;
      case 8:
        updateBatteryReading();
        break;
      case 9:
        currentPage = PAGE_TEST;
        setSignalGenOutput(g_testGenState != TEST_SIG_OFF, g_testGenFreqKHz);
        break;
      case 10:
        settingsEditingThresh = false;
        currentPage = PAGE_MAIN;
        break;
    }
  }
  else if (currentPage == PAGE_TEST) {
    g_testActiveField = (TestField)(((int)g_testActiveField + 1) % 4);
  }
}

void handleDoubleClick() {
  wakeOLED();
  settingsEditingThresh = false;
  if (g_playSource != SRC_LIVE_ADC) stopPlayback();

  // Cycle UI Pages: MAIN -> SPECTRUM -> PLAYER -> SETTINGS -> TEST -> MAIN
  currentPage = (UIPage)((currentPage + 1) % 5);
  if (currentPage == PAGE_TEST) {
    setSignalGenOutput(g_testGenState != TEST_SIG_OFF, g_testGenFreqKHz);
  } else {
    setSignalGenOutput(false, 0); // Always mute generator when exiting TEST page!
  }
  if (currentPage == PAGE_PLAYER) {
    scanSDWavFiles();
  }
}

void handleKnobHold() {
  wakeOLED();
  if (currentPage == PAGE_PLAYER) {
    g_playerMode = (PlayerListenMode)(((int)g_playerMode + 1) % 4);
    logPrintf("[PLAYER] Mode switched to: %s\n", getPlayerModeTag(g_playerMode));
  } else if (currentPage == PAGE_SETTINGS) {
    settingsEditingThresh = false;
    currentPage = PAGE_MAIN;
  } else if (currentPage == PAGE_TEST) {
    g_testGenState = (g_testGenState == TEST_SIG_OFF) ? TEST_SIG_ON : TEST_SIG_OFF;
    setSignalGenOutput(g_testGenState != TEST_SIG_OFF, g_testGenFreqKHz);
  } else {
    saveRingBufferToSD(false);
  }
}

// ------------------------------------------------------------------------------
// MAIN LOOP (CORE 0)
// ------------------------------------------------------------------------------
void loop() {
  bool needsRedraw = false;
  unsigned long now = millis();

  // Auto-sweep test signal generator if in TEST page and SWP mode
  if (currentPage == PAGE_TEST && g_testGenState == TEST_SIG_SWEEP) {
    if (now - g_lastSweepStepMs >= 150) {
      g_lastSweepStepMs = now;
      g_testGenFreqKHz += g_sweepDirection;
      if (g_testGenFreqKHz >= 96) {
        g_testGenFreqKHz = 96;
        g_sweepDirection = -1;
      } else if (g_testGenFreqKHz <= 35) {
        g_testGenFreqKHz = 35;
        g_sweepDirection = 1;
      }
      setSignalGenOutput(true, g_testGenFreqKHz);
      needsRedraw = true;
    }
  }

  // 0. Service SD Streaming & Wi-Fi Client Requests cleanly on Core 0
  if (g_playSource == SRC_SD_WAV) {
    pumpSDStreamFromCore0();
  } else {
#if USE_SDFAT
    if (playSdFile.isOpen()) playSdFile.close();
#else
    if (playSdFile) playSdFile.close();
#endif
  }
  if (wifiActive) webServer.handleClient();

  // 1. Run 256-pt FFT whenever Core 1 captured a fresh frame
  if (fftCaptureReady) {
    fftEngine.compute(fftCaptureBuf, fftSampleRateHz);
    g_peakFreqKHz = fftEngine.peakFreqKHz;
    fftCaptureReady = false;
  }

  // 2. Quadrature Rotary Encoder
  uint8_t encState = (digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT);
  if (encState != lastEncState) {
    if ((lastEncState == 0b00 && encState == 0b01) ||
        (lastEncState == 0b01 && encState == 0b11) ||
        (lastEncState == 0b11 && encState == 0b10) ||
        (lastEncState == 0b10 && encState == 0b00)) {
      encoderTicks++;
    } else if ((lastEncState == 0b00 && encState == 0b10) ||
               (lastEncState == 0b10 && encState == 0b11) ||
               (lastEncState == 0b11 && encState == 0b01) ||
               (lastEncState == 0b01 && encState == 0b00)) {
      encoderTicks--;
    }
    lastEncState = encState;

    if (abs(encoderTicks) >= TICKS_PER_DETENT) {
      int step = -(encoderTicks / TICKS_PER_DETENT); // Reversed so CW increments forward
      encoderTicks %= TICKS_PER_DETENT;
      handleEncoderTurn(step);
      needsRedraw = true;
    }
  }

  // 3. Rotary Encoder Switch (Single Click / Double Click / Hold)
  bool swReading = digitalRead(PIN_ENC_SW);
  if (swReading == LOW && swLastReading == HIGH) {
    swDownMs = now;
    swHoldFired = false;
    wakeOLED();
  } else if (swReading == LOW && !swHoldFired) {
    if (now - swDownMs >= 700) {
      swHoldFired = true;
      swWaitDouble = false;
      handleKnobHold();
      needsRedraw = true;
    }
  } else if (swReading == HIGH && swLastReading == LOW) {
    if (!swHoldFired && (now - swDownMs > 30)) {
      if (swWaitDouble && (now - swLastClickUpMs < 320)) {
        swWaitDouble = false;
        handleDoubleClick();
        needsRedraw = true;
      } else {
        swWaitDouble = true;
        swLastClickUpMs = now;
      }
    }
  }
  swLastReading = swReading;

  if (swWaitDouble && (now - swLastClickUpMs >= 320)) {
    swWaitDouble = false;
    handleSingleClick();
    needsRedraw = true;
  }

  // 4. Onboard ESP32 BOOT Button (Tap = Save 5s WAV, Hold = Toggle AUTO-PAM)
  bool bootReading = digitalRead(PIN_BOOT_BTN);
  if (bootReading == LOW && bootLastReading == HIGH) {
    bootDownMs = now;
    bootHoldFired = false;
    wakeOLED();
  } else if (bootReading == LOW && !bootHoldFired) {
    if (now - bootDownMs >= 700) {
      bootHoldFired = true;
      autoModeEnabled = !autoModeEnabled;
      autoTriggerPending = false;
      drawUI(autoModeEnabled ? "AUTO PAM ON" : "MANUAL MODE");
      delay(450);
      needsRedraw = true;
    }
  } else if (bootReading == HIGH && bootLastReading == LOW) {
    if (!bootHoldFired && (now - bootDownMs > 30)) {
      saveRingBufferToSD(false);
      needsRedraw = true;
    }
  }
  bootLastReading = bootReading;

  // 5. Autonomous Overnight Bat Call Trigger Engine
  if (autoModeEnabled && adcClockAlive && sdMounted && g_playSource == SRC_LIVE_ADC) {
    if (!autoTriggerPending && (now > autoCooldownUntilMs)) {
      if (g_ultraPeak > g_trigThreshold) {
        autoTriggerPending = true;
        autoTriggerTimeMs = now;
        logPrintf("[AUTO] Trigger (Ultra=%.0f | Peak=%.1fkHz)!\n", g_ultraPeak, g_peakFreqKHz);
        needsRedraw = true;
      }
    } else if (autoTriggerPending && (now - autoTriggerTimeMs >= 2000UL)) {
      autoTriggerPending = false;
      saveRingBufferToSD(true);
      autoCooldownUntilMs = millis() + 3000UL;
      needsRedraw = true;
    }

    if (!oledSleeping && oledPresent && (now - lastUserActivityMs > 60000UL)) {
      oledSleeping = true;
      display.clearDisplay();
      display.display();
      display.ssd1306_command(SSD1306_DISPLAYOFF);
    }
  }

  // 6. Refresh OLED (~16 fps) & Serial Telemetry
  static unsigned long lastOledRefresh = 0;
  if (needsRedraw || (now - lastOledRefresh >= 60)) {
    lastOledRefresh = now;
    drawUI();
  }

  static unsigned long lastSerialLog = 0;
  if (now - lastSerialLog >= 1000) {
    lastSerialLog = now;
    if (adcClockAlive) {
      logPrintf("[TELEM] %uHz | PK:%.1fkHz | VU:%d%% | Ultra:%.0f | Page:%d\n",
                measuredSampleRate, g_peakFreqKHz, g_vuPeak, g_ultraPeak, (int)currentPage + 1);
    }
  }
}
