#pragma once

#include <Arduino.h>

// ------------------------------------------------------------------------------
// HARDWARE PINOUT (VERIFIED CARRIER v3.3-SC26)
// ------------------------------------------------------------------------------
#define HW_BOARD_REV      "v3.3-SC26"
static const char FW_BUILD_SIG[] PROGMEM = "ADW-V33-SC26-SANSHRAY";
#define OLED_WIDTH        128
#define OLED_HEIGHT       32
#define OLED_RESET        -1
#define OLED_ADDR         0x3C
#define OLED_ROTATION     0
#define PIN_I2C_SDA       8
#define PIN_I2C_SCL       9

#define PIN_ENC_CLK       6
#define PIN_ENC_DT        5
#define PIN_ENC_SW        4
#define PIN_BOOT_BTN      0

// Carrier PCB Dedicated Tactile Buttons (Active LOW, internal pull-up)
#define PIN_BTN_REC       7     // SW1: Toggle Record Start / Stop
#define PIN_BTN_DISP      3     // SW2: Display Mode Cycle / Screen Wake
#define PIN_BTN_UP        1     // SW3: Up / Value Increment (+)
#define PIN_BTN_DOWN      21    // SW4: Down / Value Decrement (-)

#define PIN_SD_CS         10
#define PIN_SD_MOSI       11
#define PIN_SD_SCK        12
#define PIN_SD_MISO       13

#define I2S_ADC_PORT      I2S_NUM_0
#define PIN_ADC_BICK      39
#define PIN_ADC_DATA      40
#define PIN_ADC_LRCK      41
#define ADC_SAMPLE_RATE   192000

#define I2S_DAC_PORT      I2S_NUM_1
#define PIN_DAC_BCK       15
#define PIN_DAC_DIN       16
#define PIN_DAC_LCK       17
#define DAC_SAMPLE_RATE   48000

#define PIN_SIG_GEN       18    // ESP32-S3 Hardware PWM Signal Generator (35-100 kHz)
#define PIN_BATTERY_ADC   2     // ESP32-S3 GPIO 2 (ADC1 Ch1) for 100k+100k Battery Voltage Divider
#define BAT_DIVIDER_RATIO 2.0f  // 1:2 Divider multiplier (100k + 100k)

// ------------------------------------------------------------------------------
// BUFFER & MEMORY SIZING (ESP32-S3-N16R8 8MB OPI PSRAM)
// ------------------------------------------------------------------------------
#define ADC_READ_FRAMES   512
#define DAC_OUT_FRAMES    (ADC_READ_FRAMES / 4)

#define RING_SECONDS      5
#define RING_FRAMES       (ADC_SAMPLE_RATE * RING_SECONDS)
#define RING_SAMPLES      (RING_FRAMES * 2)     // 3,840,000 bytes (3.66 MiB)

#define TE_SNAP_SECONDS   2
#define TE_SNAP_FRAMES    (ADC_SAMPLE_RATE * TE_SNAP_SECONDS)
#define TE_SNAP_SAMPLES   (TE_SNAP_FRAMES * 2)  // 1,536,000 bytes (1.46 MiB)

#define SD_STREAM_SAMPLES 131072                // 262,144 bytes (256 KiB SPSC ring)
#define MAX_WAV_LIST      256                   // Expanded from 64 to 256 for large SD card libraries

// ------------------------------------------------------------------------------
// SHARED ENUMS & STRUCTS
// ------------------------------------------------------------------------------
enum UIPage : uint8_t {
  PAGE_MAIN     = 0,
  PAGE_SPECTRUM = 1,
  PAGE_PLAYER   = 2,
  PAGE_SETTINGS = 3,
  PAGE_TEST     = 4
};

enum TestField : uint8_t {
  TEST_FIELD_GEN   = 0,
  TEST_FIELD_HET   = 1,
  TEST_FIELD_STATE = 2,
  TEST_FIELD_VOL   = 3
};

enum TestGenState : uint8_t {
  TEST_SIG_OFF   = 0,
  TEST_SIG_ON    = 1,
  TEST_SIG_SWEEP = 2
};

enum VolumeCurve : uint8_t {
  VOL_CURVE_LOG = 0, // Perceptual audio taper (smooth quadratic): clean, fine earphone control
  VOL_CURVE_LIN = 1  // Standard linear voltage taper
};

enum ListenMode : uint8_t {
  LISTEN_HET = 0,  // Dual-Channel Heterodyne (or DR when 0 kHz)
  LISTEN_FD  = 1   // Broadband Frequency Division (/10)
};

enum InputChannelMode : uint8_t {
  IN_LR_STEREO = 0, // Both L & R active
  IN_L_ONLY    = 1, // Duplicate Left mic to both channels
  IN_R_ONLY    = 2  // Duplicate Right mic to both channels
};

enum PlaybackSource : uint8_t {
  SRC_LIVE_ADC = 0,
  SRC_TE_RING  = 1,
  SRC_SD_WAV   = 2
};

enum PlayerListenMode : uint8_t {
  PLAY_HET_1X = 0,  // Real-time 1x speed + Live Heterodyne Knob + Spectrogram!
  PLAY_FD_1X  = 1,  // Real-time 1x speed + Broadband Frequency Division /10
  PLAY_TE_10X = 2,  // 10x Time Expansion (Slow Motion)
  PLAY_TE_5X  = 3   // 5x Time Expansion
};

enum EditField : uint8_t {
  FIELD_F1 = 0,
  FIELD_VOL = 1,
  FIELD_GAIN = 2,
  FIELD_F2 = 3
};

struct WavEntry {
  char name[64];
  uint32_t sizeBytes;
};

struct __attribute__((packed)) WavHeader {
  char     riff[4]       = {'R','I','F','F'};
  uint32_t chunkSize;
  char     wave[4]       = {'W','A','V','E'};
  char     fmt[4]        = {'f','m','t',' '};
  uint32_t subchunk1Size = 16;
  uint16_t audioFormat   = 1;
  uint16_t numChannels   = 2;
  uint32_t sampleRate    = ADC_SAMPLE_RATE;
  uint32_t byteRate      = ADC_SAMPLE_RATE * 2 * 2;
  uint16_t blockAlign    = 4;
  uint16_t bitsPerSample = 16;
  char     data[4]       = {'d','a','t','a'};
  uint32_t dataSize;
};
