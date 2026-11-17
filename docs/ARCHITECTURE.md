# System Architecture & Signal Path — AtiDhwani Bat PAM Node

AtiDhwani is an open-source, modular **Passive Acoustic Monitoring (PAM)** hardware platform engineered for bioacoustics researchers, conservationists, and wildlife ecologists studying bats and other ultrasonic vocalizing fauna.

---

## 1. High-Level Block Diagram

```mermaid
flowchart TD
    subgraph Power["Power Subsystem"]
        BATT["3.7V Li-Ion Battery"] --> TP["TP4056 Charger (USB-C 5V 1A)"]
        TP --> SW["SPDT Power Switch"]
        SW --> BOOST["XL3608 5V Step-Up Boost"]
        SW --> GAUGE["100k/100k Divider + 2.2nF Filter"]
        BOOST --> V5["5V System Rail"]
        GAUGE --> IO2["ESP32-S3 GPIO 2 (ADC1)"]
    end

    subgraph Acoustic["Ultrasonic Front-End (Left Bay)"]
        LDO["AP2127K-2.8V Low-Noise LDO"] --> MIC1["Knowles SPV0142 (10-80kHz)"]
        LDO --> MIC2["Infineon IM70A135 (High-SNR)"]
        MIC1 --> C1["2.2µF AC Coupling"]
        MIC2 --> C2["2.2µF AC Coupling"]
        C1 --> J_MIC["J_MIC_OUT (1x03 Header)"]
        C2 --> J_MIC
        J_MIC --> RCA["WM8782 ADC Analog RCA Inputs"]
    end

    subgraph Digital_ADC["24-Bit Audio ADC (WM8782)"]
        RCA --> ADC["WM8782 ADC Core (Master Mode, 192kHz)"]
        ADC -- "LRCK (IO41)" --> ESP
        ADC -- "DATA (IO40)" --> ESP
        ADC -- "BICK (IO39)" --> ESP
    end

    subgraph Core["ESP32-S3 Dual-Core Xtensa LX7 (240MHz, 8MB PSRAM)"]
        ESP["ESP32-S3 Core & DMA Engines"]
        ESP --> DSP["Real-Time DSP (Heterodyne, Div-by-N, FFT)"]
        ESP --> REC["FAT32 WAV File Writer (Ring Buffer)"]
    end

    subgraph UI_Right["User Interface & Peripherals (Right Bay)"]
        ESP -- "I2C (IO8/SDA, IO9/SCK)" --> OLED["0.91\" SSD1306 OLED (128x32)"]
        ESP -- "SPI (IO10-13)" --> SD["MicroSD Push-Push Slot (Facing Right ->)"]
        DSP -- "I2S TX (IO15-17)" --> DAC["PCM5102A 32-Bit DAC"]
        DAC --> PHONE["3.5mm Headphone Jack (Top-Right ->)"]
        ENC["EC11 Encoder (IO4-6)"] --> ESP
        BTNS["Tactile Buttons (IO1, 3, 7, 21)"] --> ESP
    end
```

---

## 2. Ultrasonic Analog Front-End

Most microbats emit frequency-modulated (FM) or constant-frequency (CF) echolocation calls between **15 kHz and 120 kHz**. Capturing these signals requires wideband transducers, an ultra-quiet analog supply, and wideband analog-to-digital conversion:

* **Dual MEMS Microphones:**
  * **Knowles SPV0142LR5H-1:** Bottom-port analog MEMS transducer with verified acoustic response extending beyond 80 kHz.
  * **Infineon IM70A135:** Ultra-high SNR (70 dB(A)) low-distortion bottom-port transducer, providing excellent low-noise sensitivity for distant or faint echolocation passes.
* **Low-Noise Supply (`U4`):**
  * The microphones are powered by an **AP2127K-2.8** low-dropout linear regulator, delivering a clean 2.8V rail with high Power Supply Rejection Ratio (PSRR) to eliminate digital switching noise from the 5V boost converter and ESP32 WiFi/BLE radios.
* **AC Coupling:**
  * $2.2\,\mu\text{F}$ 0805 X7R capacitors decouple the DC bias voltage from the microphone output buffers before routing to `J_MIC_OUT`.

---

## 3. High-Resolution Audio ADC (WM8782)

Digital conversion is performed by an external **Cirrus Logic / Wolfson WM8782** 24-bit high-performance stereo ADC:
* **Sampling Rate:** Configured for **192 kHz** or **96 kHz** stereo sampling (Nyquist bandwidth up to 96 kHz).
* **Clocking Mode:** Hardware jumpered in **Master Mode** with an on-board $24.576\text{ MHz}$ low-jitter crystal oscillator. The WM8782 generates `BICK` and `LRCK`, clocking audio data into the ESP32-S3 I2S peripheral acting as an I2S Slave. This guarantees zero audio clock drift and jitter-free acoustic streaming.

---

## 4. Digital Signal Processing (DSP) & Heterodyne Listening

Because microbat ultrasound is inaudible to human ears, the firmware incorporates real-time audio transformation engines:
1. **Heterodyne Down-Conversion:**
   * Incoming ultrasonic audio is multiplied in real-time by a local sinusoidal oscillator tunable from $15\text{ kHz}$ to $90\text{ kHz}$ via the EC11 rotary encoder.
   * The resulting sum and difference frequencies are passed through a digital low-pass filter, shifting bat calls into the human audible band ($300\text{ Hz} - 4\text{ kHz}$) with zero noticeable latency.
2. **Frequency Division:**
   * Zero-crossing detection engine scales down ultrasonic frequencies by a selectable factor (e.g., $1/10$ or $1/16$).
3. **Real-Time 128-Point FFT:**
   * An optimized Radix-4 FFT runs on Core 1 of the ESP32-S3, computing spectral peak frequencies and rendering live audio VU meters and waterfall spectrograms on the 0.91" OLED display.

---

## 5. Storage & High-Fidelity Playback

* **MicroSD FAT32 WAV Recording:**
  * Raw 16-bit or 24-bit high-speed ultrasonic audio chunks are streamed through an 8MB PSRAM double-buffer ring buffer, guaranteeing drop-free sustained writes to FAT32 MicroSD cards via standard SPI bus at 20–40 MHz.
* **32-Bit I2S DAC (PCM5102A):**
  * Synthesized audible audio (Heterodyne or Pitch-Shifted) is streamed via hardware I2S TX to a **Burr-Brown / TI PCM5102A** DAC.
  * The DAC features an integrated charge pump providing a ground-centered $2.1\text{ V}_{\text{RMS}}$ stereo output, driving standard $32\,\Omega$ field headphones directly through the top-right 3.5mm stereo jack.
