#pragma once

#include <Arduino.h>
#include <math.h>

#define FFT_SIZE       256
#define FFT_HALF       (FFT_SIZE / 2)

class UltrasonicFFT {
public:
  float hannWindow[FFT_SIZE];
  float re[FFT_SIZE];
  float im[FFT_SIZE];
  float spectrumBins[FFT_HALF];     // Smoothed linear magnitudes (16-bit audio scale)
  float peakFreqKHz = 0.0f;
  float peakStrength = 0.0f;

  // Dynamic display bounds (15-96 kHz for 192kHz Nyquist, 1-Nyquist for 48k/96k WAVs)
  int   minBin = 20;
  int   maxBin = 127;
  float dispMinKHz = 15.0f;
  float dispMaxKHz = 96.0f;

  void init() {
    for (int i = 0; i < FFT_SIZE; i++) {
      hannWindow[i] = 0.5f * (1.0f - cosf((2.0f * M_PI * i) / (float)(FFT_SIZE - 1)));
    }
    memset(spectrumBins, 0, sizeof(spectrumBins));
  }

  int freqToX(float freqKHz, int screenWidth) const {
    if (dispMaxKHz <= dispMinKHz) return 0;
    float norm = (freqKHz - dispMinKHz) / (dispMaxKHz - dispMinKHz);
    return constrain((int)roundf(norm * (float)(screenWidth - 1)), 0, screenWidth - 1);
  }

  // Runs 256-pt Radix-2 Cooley-Tukey FFT on 16-bit scaled float block at given sampleRateHz
  void compute(const float* samples, float sampleRateHz = 192000.0f) {
    if (sampleRateHz < 8000.0f) sampleRateHz = 192000.0f;
    float binHz = sampleRateHz / (float)FFT_SIZE;

    if (sampleRateHz >= 120000.0f) {
      dispMinKHz = 15.0f;
      dispMaxKHz = 96.0f;
    } else {
      dispMinKHz = 1.0f;
      dispMaxKHz = (sampleRateHz * 0.47f) / 1000.0f;
    }

    minBin = constrain((int)roundf((dispMinKHz * 1000.0f) / binHz), 1, FFT_HALF - 2);
    maxBin = constrain((int)roundf((dispMaxKHz * 1000.0f) / binHz), minBin + 1, FFT_HALF - 1);

    // 1. Remove local block DC mean + apply Hann window
    float mean = 0.0f;
    for (int i = 0; i < FFT_SIZE; i++) mean += samples[i];
    mean /= (float)FFT_SIZE;

    for (int i = 0; i < FFT_SIZE; i++) {
      re[i] = (samples[i] - mean) * hannWindow[i];
      im[i] = 0.0f;
    }

    // 2. Bit-reversal permutation
    int j = 0;
    for (int i = 1; i < FFT_SIZE; i++) {
      int bit = FFT_SIZE >> 1;
      while (j & bit) {
        j ^= bit;
        bit >>= 1;
      }
      j ^= bit;
      if (i < j) {
        float tmpR = re[i]; re[i] = re[j]; re[j] = tmpR;
      }
    }

    // 3. Butterfly stages
    for (int len = 2; len <= FFT_SIZE; len <<= 1) {
      float ang = -2.0f * M_PI / (float)len;
      float wlenR = cosf(ang);
      float wlenI = sinf(ang);
      int halfLen = len >> 1;

      for (int i = 0; i < FFT_SIZE; i += len) {
        float wR = 1.0f, wI = 0.0f;
        for (int k = 0; k < halfLen; k++) {
          int even = i + k;
          int odd  = even + halfLen;
          float tR = re[odd] * wR - im[odd] * wI;
          float tI = re[odd] * wI + im[odd] * wR;

          re[odd] = re[even] - tR;
          im[odd] = im[even] - tI;
          re[even] += tR;
          im[even] += tI;

          float nextWR = wR * wlenR - wI * wlenI;
          wI = wR * wlenI + wI * wlenR;
          wR = nextWR;
        }
      }
    }

    // 4. Magnitude extraction & peak detection (within active spectrum range)
    int bestBin = minBin;
    float bestMag = 0.0f;

    for (int k = 0; k < FFT_HALF; k++) {
      float mag = sqrtf(re[k] * re[k] + im[k] * im[k]) / (float)FFT_HALF;
      if (mag > spectrumBins[k]) {
        spectrumBins[k] = 0.72f * mag + 0.28f * spectrumBins[k];
      } else {
        spectrumBins[k] = 0.30f * mag + 0.70f * spectrumBins[k];
      }

      if (k >= minBin && k <= maxBin && mag > bestMag) {
        bestMag = mag;
        bestBin = k;
      }
    }

    peakStrength = bestMag;
    if (bestMag > 4.0f) { // Clean noise floor threshold for 16-bit normalized samples
      float kExact = (float)bestBin;
      if (bestBin > minBin && bestBin < maxBin) {
        float m0 = sqrtf(re[bestBin - 1] * re[bestBin - 1] + im[bestBin - 1] * im[bestBin - 1]);
        float m1 = sqrtf(re[bestBin]     * re[bestBin]     + im[bestBin]     * im[bestBin]);
        float m2 = sqrtf(re[bestBin + 1] * re[bestBin + 1] + im[bestBin + 1] * im[bestBin + 1]);
        float denom = m0 - 2.0f * m1 + m2;
        if (fabsf(denom) > 1e-3f) {
          float delta = 0.5f * (m0 - m2) / denom;
          kExact += constrain(delta, -0.5f, 0.5f);
        }
      }
      peakFreqKHz = (kExact * binHz) / 1000.0f;
    } else {
      peakFreqKHz = 0.0f;
    }
  }
};
