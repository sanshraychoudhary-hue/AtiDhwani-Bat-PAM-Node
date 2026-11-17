# AtiDhwani Hardware Verification & Bring-Up Test Suite

This directory contains a complete, staged hardware verification test suite for the **AtiDhwani Bat PAM Node**. When assembling a newly fabricated carrier PCB or prototyping on a breadboard, flash these standalone Arduino sketches sequentially to isolate and verify each subsystem independently.

---

## 1. Test Suite Index

| Test Name | Subsystem Verified | Key Pins Tested | Description |
| :--- | :--- | :--- | :--- |
| **`test_01_esp32_psram`** | ESP32-S3 Core & PSRAM | Internal | Verifies CPU clock speed (240 MHz) and runs a rigorous 8MB Octal PSRAM read/write memory integrity and throughput test. |
| **`test_02_oled`** | 0.91" SSD1306 Display | `GPIO 8` (SDA), `GPIO 9` (SCK) | Probes I2C bus address `0x3C`, displays boot logos, text animations, and tests display refresh at 30+ FPS. |
| **`test_03_rotary_encoder`** | EC11 Rotary Encoder | `GPIO 4` (SW), `GPIO 5` (DT), `GPIO 6` (CLK) | Validates quadrature decoding state machine, directional rotation (+ / - count), and center push-button switch. |
| **`test_04_buttons`** | Tactile Field Switches | `GPIO 7` (REC), `GPIO 3` (DISP), `GPIO 1` (UP), `GPIO 21` (DOWN) | Tests active-low internal pull-up debounce logic on all four field pushbuttons with real-time serial logging. |
| **`test_05_sd_card`** | MicroSD Storage | `GPIO 10` (CS), `GPIO 11` (MOSI), `GPIO 12` (SCK), `GPIO 13` (MISO) | Initializes FAT32 filesystem, measures raw block write speed (MB/s), and creates test files to ensure reliable drop-free audio recording. |
| **`test_06_wm8782_adc`** | WM8782 24-Bit ADC | `GPIO 39` (BICK), `GPIO 40` (DATA), `GPIO 41` (LRCK) | Configures ESP32 I2S0 peripheral in Slave Receiver mode, receives 192 kHz audio streams from ADC, and prints peak amplitude metrics. |
| **`test_07_pcm5102_dac`** | PCM5102A 32-Bit DAC | `GPIO 15` (BCK), `GPIO 16` (DIN), `GPIO 17` (LCK) | Generates continuous 1 kHz sine waves and frequency sweeps (20 Hz - 20 kHz) to test 3.5mm stereo headphone output and verify hardware configuration bridges (`H1L=GND`, `H2L=GND`, `H3L=3.3V`, `H4L=GND`). |
| **`test_08_integrated_ui_sd`** | Integrated UI & Logging | All UI + SD pins | Runs the complete graphical UI on the OLED while allowing menu navigation via encoder and logging events to MicroSD. |
| **`test_esp32_ultrasonic_generator`** | Ultrasonic Test Generator | `GPIO 18` | Generates 35 kHz - 100 kHz hardware PWM test chirps for bench-testing bat detection algorithms without requiring live bats. |
| **`test_full_system_adc_dac_sd`** | Full System Pipeline | All pins | Executes end-to-end audio pipeline: 192 kHz ADC capture $\rightarrow$ Heterodyne DSP $\rightarrow$ 48 kHz DAC headphone output $\rightarrow$ simultaneous FAT32 WAV write. |
| **`test_layer1_full_check`** | Layer 1 Diagnostic Suite | All buses | Automated self-test script reporting PASS/FAIL status for I2C, SPI, I2S0, I2S1, and ADC telemetry. |

---

## 2. Audio Reference Samples

The `test_audio_samples/` folder includes high-fidelity WAV test files for bench verification:
* `real_bat_20kHz_for_Realtek_48k_stereo.wav`: Low-frequency microbat echolocation call.
* `real_bat_43kHz_loud_192k_stereo.wav`: Pipistrelle echolocation pass recorded at 192 kHz.
* `synth_41kHz_bat_chirps_192k_stereo.wav`: Calibrated synthetic downward-frequency FM chirps (60 kHz down to 35 kHz).

---

## 3. Recommended Bring-Up Workflow

For a newly assembled PCB:
1. **Power & Rails:** Measure with DMM before inserting socketed ICs:
   * Verify $+5.0\text{ V}$ rail at XL3608 output.
   * Verify $+3.3\text{ V}$ rail at ESP32 socket pin 1.
   * Verify $+2.8\text{ V}$ ultra-quiet rail at `U4` output for the MEMS microphones.
2. **Core & Peripherals:**
   * Plug in ESP32-S3 module and run `test_01_esp32_psram`.
   * Plug in OLED display and run `test_02_oled`.
   * Test buttons & encoder using `test_03_rotary_encoder` and `test_04_buttons`.
   * Insert MicroSD card and run `test_05_sd_card`.
3. **Audio Subsystems:**
   * Plug in PCM5102A DAC module, connect headphones, and run `test_07_pcm5102_dac` (verify clean audio tone).
   * Plug in WM8782 ADC module and run `test_06_wm8782_adc`.
4. **End-to-End Integration:**
   * Run `test_full_system_adc_dac_sd` and `test_layer1_full_check`.
