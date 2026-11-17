# Esperdyne Bat Detector & PAM Node — Complete User Manual (v2.0)

Firmware Location: [`Esperdyne_Full_Firmware.ino`](Esperdyne_Full_Firmware.ino)  
Hardware Target: **ESP32-S3-N16R8** (16 MB Flash, 8 MB OPI PSRAM) + **WM8782 ADC** (192 kHz 24-bit Master) + **PCM5102A DAC** + **0.91" OLED** + **MicroSD**

---

## 1. System-Wide Controls (Work on Any Screen)

You only have **two physical controls**—the big **EC11 Rotary Knob** and the small onboard **`BOOT` button (`GPIO 0`)** on the ESP32-S3 board.

| Input Gesture | Global Action (Works Everywhere) |
| :--- | :--- |
| **2× Double-Click Knob** *(two clicks within 0.32 s)* | **Switch Page:** `MAIN` ➔ `SPECTRUM` ➔ `SD PLAYER` ➔ `SETTINGS` ➔ `ADC TEST` ➔ `MAIN` *(SD card playback stays running smoothly across pages!)* |
| **Tap `BOOT` Button** *(short press < 0.7 s)* | **Instant Save WAV:** Writes the last **5 seconds** of raw 192 kHz stereo audio from PSRAM to `/REC###.WAV` on the SD card. |
| **Hold `BOOT` Button** *(hold ≥ 0.7 s)* | **Toggle `AUTO-PAM`:** Arms or disarms autonomous overnight ultrasonic trigger recording. |
| **Any Knob Turn / Click** *(while screen is asleep)* | **Wake OLED:** Turns the display back on and resets the 60-second overnight sleep timer. |

> [!NOTE]
> **Clockwise (`CW`) Rotation:** Turning the knob **clockwise** increases numbers (`F1k`, `VOL`, `GNx`, `Thresh`) and scrolls **down** through files and settings menus.  
> **Raw Recording Guarantee:** All `.WAV` files saved to the MicroSD card are **always** raw, full-spectrum **192 kHz Stereo 16-bit PCM (0–96 kHz)**, regardless of whether your headphones are listening in `DR`, `HET`, or `FD`.

---

## 2. Page-by-Page Menu & Button Guide

Double-click the rotary knob to cycle through the **5 pages**:

---

### Page 1: `MAIN` (Live Detector & Monitor)

#### OLED Layout
```text
[F1k]  VOL    GNx    F2k     <-- White highlight = active parameter turned by knob
 45     80     32     45     <-- Ch1 kHz (or DR) | Volume % | Gain | Ch2 kHz
MX  #00 [||||||    ]  MAN    <-- Route | Next File # | Live VU Bar | Mode / SD Status
```
*(In Frequency Division mode, `F1k` changes to `FD` showing `/10`, and `F2k` changes to `PKk` showing live ultrasonic peak kHz).*

#### Controls on `MAIN`
| Control | Action |
| :--- | :--- |
| **Turn Knob** | Adjusts highlighted field (`F1k`, `VOL`, `GNx`, or `F2k`). *In `MX` mode, turning `F1k` also links `F2k` automatically.* |
| **1× Short Click** | Cycles active box: **`F1k` ➔ `VOL` ➔ `GNx`** *(adds **`F2k`** when `Route` is `TRUE STEREO`; cycles **`VOL` ↔ `GNx`** in `FD /10` mode)*. |
| **Hold Knob (≥ 0.7 s)** | **Save 5 s WAV** (`/REC###.WAV`) to MicroSD. |

#### Field Ranges & Bottom Badges
* **`F1k` / `F2k`:** `DR` (`0 kHz` Direct audible pass-through) or `10`–`90 kHz` Heterodyne in **2 kHz** steps (`10 ➔ DR` when turning down past 10; `DR ➔ 10` when turning up from DR).
* **`VOL`:** `0`–`100%` in **5%** steps (default `80%`).
* **`GNx`:** Digital boost: `1×`, `2×`, `4×`, `8×`, `16×`, `32×`, `64×`, `128×`, `256×`, `512×` (default `32×`).
* **Bottom-Left Route Badge:**
  * `MX` — Mono Mix (sums L+R mics; `F1k` sets both channels)
  * `ST` — True Dual Stereo (Left ear = Left mic @ `F1k`, Right ear = Right mic @ `F2k`)
  * `L1` — Left Mic Only (copies Left RCA input to both ears, muting empty Right jack)
  * `R1` — Right Mic Only (copies Right RCA input to both ears, muting empty Left jack)
* **Bottom-Center Bar:** Live audio peak VU meter *(prints **`NO-ADC`** if WM8782 I2S clocks are not detected on GPIO 39/40/41)*.
* **Bottom-Right Status Badge:**
  * `MAN` — Manual recording mode
  * `AUTO` *(inverted)* — Overnight ultrasonic trigger armed
  * `TRIG` *(inverted)* — Call detected! Waiting 2 s post-trigger to save WAV
  * `WIFI` *(inverted)* — Wi-Fi hotspot (`192.168.4.1`) active
  * `NOSD` — No MicroSD card detected

#### How to Hear Audible (`DR`) & Ultrasonic (`HET`) at the Same Time
1. Go to **Page 4 (`SETTINGS`)** ➔ set **`Route : TRUE STEREO`** (`ST`).
2. Return to **`MAIN`**, set **`F1k`** to **`45`** (Left ear listens to 45 kHz ultrasonic bats).
3. Click through to **`F2k`** and turn down past `10` into **`DR`** (Right ear listens to normal audible voice/birds).

---

### Page 2: `SPECTRUM` (Live 15–90 kHz Ultrasonic FFT Analyzer)

#### OLED Layout
```text
PK:45.3k          HET:45k    <-- Top: Parabolic FFT Peak kHz | Current Mode/Dial
____||__||___▼___________    <-- Bottom: Live 256-pt FFT bars + HET Tuning Cursor (▼)
```

* Uses a **256-point Radix-2 FFT** (`750 Hz/bin` @ 192 kHz) spanning **15.0 kHz – 90.0 kHz** for 192 kHz audio *(automatically switches to **1 kHz – Nyquist** if playing back a 48 kHz or 96 kHz WAV file from SD)*.
* The triangle cursor **`▼`** sits directly above the spectrum frequency your `F1k` Heterodyne dial is currently tuned to.

#### Controls on `SPECTRUM`
| Control | Action |
| :--- | :--- |
| **Turn Knob** | Fine-tunes **`F1k`** in **1 kHz steps** (`DR` or `10`–`90 kHz`) so you can slide `▼` straight onto a peak bar. |
| **1× Short Click** | Quick-toggles **`HET`** ↔ **`FD /10`** *(or `HET 1x` ↔ `FD /10` if SD audio is playing)*. |
| **Hold Knob (≥ 0.7 s)** | **Save 5 s WAV** (`/REC###.WAV`) to MicroSD. |

---

### Page 3: `SD PLAYER` (MicroSD Browser, Playback & Live Spectrogram)

Scans `/` for up to **64 `.WAV` files** (sorted alphabetically descending so your newest `REC###.WAV` appears first). Playback **automatically loops** cleanly so you can inspect short bat passes without restarting.

#### View A: File Browser (When Stopped)
```text
SD [HET 1x]          1/12    <-- Active Playback Mode | Selected / Total Files
> REC011.WAV                 <-- Selected Filename (up to 18 chars shown on OLED)
3.66MB |1x:Ply H:Mod         <-- File Size | Quick Control Hint
```

#### View B: Live Playback Spectrogram (While Playing)
```text
HET:43k PK:43.5k      45%    <-- Dial/Mode | Live FFT Peak inside WAV | Loop %
____||_|||____▼__________    <-- Real-time FFT Spectrum Bars + HET Cursor (▼)
```

#### Controls on `SD PLAYER`
| Control | When Stopped (Browsing) | When Playing (Spectrogram View) |
| :--- | :--- | :--- |
| **Turn Knob** | Scroll through `.WAV` files | **`HET 1x` mode:** Tunes `F1k` (**1 kHz steps**).<br>**`FD /10`, `TE 10x`, `TE 5x`:** Adjusts **`VOL`** (`0–100%`). |
| **1× Short Click** | **Play** selected `.WAV` file | **Stop** playback & return to browser |
| **Hold Knob (≥ 0.7 s)** | Cycle Mode: **`HET 1x` ➔ `FD /10` ➔ `TE 10x` ➔ `TE  5x`** | Switch Playback Mode **live** without stopping |
| **2× Double-Click** | Switch Page | Switch Page **while audio keeps playing** *(e.g., hop to `MAIN` to adjust `GNx` gain or `VOL`!)* |

#### The 4 SD Playback Modes
1. **`HET 1x` (Default):** Plays the `.WAV` at real-time 1× speed through the live Heterodyne mixer and renders its full FFT spectrogram so you can tune `F1k` directly onto the ultrasonic peaks.
2. **`FD /10`:** Plays at real-time 1× speed through Broadband Frequency Division (`/10`), making all frequencies 15–96 kHz audible at once.
3. **`TE 10x`:** Plays slowed down **10×** (45 kHz bat call becomes a crisp 4.5 kHz bird-like chirp) while the FFT still reads true original kHz.
4. **`TE  5x`:** Plays slowed down **5×**.

> [!TIP]
> **Gain (`GNx`) for SD Playback:** Playback applies `GNx` clamped to **`1×–64×`**. Use **`GNx: 1` or `2`** for loud pre-normalized test files (like `real_bat_43kHz_loud_192k_stereo.wav`), and **`GNx: 16` or `32`** for quiet raw field captures (`REC###.WAV`). Change `GNx` anytime by double-clicking over to `MAIN` and back!

---

### Page 4: `SETTINGS` (Field Config & Wi-Fi Control)

Scrolls 3 visible rows at a time (`>` inverted bar highlights the active setting):

| Menu Item | Options | Description |
| :--- | :--- | :--- |
| **`Mode`** | `HETERODYNE` / `FREQ DIV /10` | Switches live headphone DSP between tunable Heterodyne and broadband `/10` Frequency Division. |
| **`Route`** | `MONO MIX (MX)` / `TRUE STEREO` / `LEFT MIC ONLY` / `RIGHT MIC ONLY` | Selects how ADC Left & Right inputs are processed. Use **`LEFT MIC ONLY`** or **`RIGHT MIC ONLY`** if you only have 1 mic plugged in! |
| **`VolCrv`** | `LOG (EARS)` *(Default)* / `LINEAR` | **Perceptual Audio Taper:** Eliminates background hiss, prevents clipping, and provides smooth, fine-grained control for sensitive earphones. Switch to `LINEAR` for raw electrical line-level output. |
| **`AutoPM`** | `OFF (MANUAL)` / `ON (ARMED)` | Enables autonomous overnight recording (`AUTO-PAM`). |
| **`Thresh`** | `1000` – `25000` *(step `500`, default `4500`)* | Ultrasonic trigger threshold (`Ultra` value shown in Serial Monitor). **Click once** to lock `[4500]`, turn knob to adjust, **click again** to save. |
| **`TE 2s`** | `PLAY (10x)` | Immediately snapshots the **last 2 seconds** from your PSRAM ring buffer, plays it at 10× slow-motion with a progress bar, then resumes live monitoring. |
| **`WiFiAP`** | `OFF` / `192.168.4.1` | Turns on/off the Wi-Fi hotspot (`Esperdyne-BatNode`) for phone/laptop file access. Keep **OFF** during sensitive listening to eliminate RF noise and save battery. |
| **`SDCard`** | `<TotalMB> (<Files>)` / `RESCAN` | Shows SD card size & `.WAV` count; click to re-mount or rescan if you hot-swapped the card. |
| **`ADC Test SigGen >`** | Jump to Page 5 | Launches the integrated 35–100 kHz hardware test generator & live ADC diagnostic analyzer. |
| **`< Exit to Main`** | — | Returns immediately to Page 1 (`MAIN`). |

#### Controls on `SETTINGS`
| Control | Action |
| :--- | :--- |
| **Turn Knob** | Scrolls through menu items *(or adjusts `Thresh` value when `[bracketed]`)*. |
| **1× Short Click** | Toggles/triggers highlighted item *(or locks/unlocks `Thresh` editing)*. |
| **Hold Knob (≥ 0.7 s)** | Exits straight back to Page 1 (`MAIN`). |

---

### Page 5: `TEST` (ADC Hardware Diagnostic & Ultrasonic Signal Generator)

This screen lets you verify the WM8782 ADC frequency response from **35 kHz to 100 kHz** without needing an external signal generator or ultrasonic microphone. The ESP32-S3 hardware LEDC generator outputs on **`GPIO 18`** with safe line-level amplitude (~0.36 V RMS at ~8% duty cycle).

#### OLED Layout
```text
[G:40k]  H:38k   SIG:ON   V80    <-- Inverted Box = Active field adjusted by knob
PK:40.0k      BEAT:2.0kHz        <-- Live ADC Peak Detection | Headphone Beat Pitch
____||__||___▼___________        <-- Live 256-pt FFT Spectrum Bar Graph + HET Cursor
```

#### How to Test Your ADC (Loopback Verification)
1. **Connect 1 Wire:** Plug a standard male jumper wire from **`GPIO 18`** (bottom header, adjacent to DAC pin 17) directly into the **center hole of the ADC RCA Jack** (Red or White).
2. **Double-click the knob** until you reach **`PAGE 5 (TEST)`** (or select **`ADC Test SigGen >`** in Settings).
3. **Instant Verification:**
   * **Visual Check:** Look at the OLED. A sharp, tall spectrum bar shoots up directly at **`40 kHz`**, and the readout locks to **`PK: 40.0k`**.
   * **Acoustic Check:** Put on headphones. You will hear a clean, steady **2.0 kHz whistle** (the heterodyne downmix difference $|40\text{ kHz} - 38\text{ kHz}| = 2\text{ kHz}$).
4. **Frequency Sweep Test:**
   * With **`[G:40k]`** highlighted, turn the knob clockwise to sweep up to **`50k`**, **`60k`**, **`75k`**, **`90k`**, and **`96k`**.
   * Watch the live FFT peak bar glide across the OLED display and verify that **`PK`** tracks your generator frequency exactly!
5. **Auto-Sweep Mode (`SWP`):**
   * Click the knob twice to highlight **`SIG`**.
   * Turn the knob to select **`SWP`**.
   * The firmware will automatically sweep the test tone back and forth between **35 kHz and 96 kHz** (1 kHz every 150 ms) hands-free!
6. **Mute Generator:**
   * When you double-click out of `PAGE 5` to return to `MAIN`, the signal generator automatically turns **OFF (0 V)** so it never causes background noise during bat listening.

#### Controls on `TEST`
| Control | Action |
| :--- | :--- |
| **Turn Knob** | Adjusts highlighted field:<br>• **`G:XXk`**: Generator frequency (**35 to 100 kHz**, 1 kHz steps)<br>• **`H:XXk`**: Heterodyne receiver dial (**`DR`** or **10 to 90 kHz**)<br>• **`SIG`**: Generator state (**`ON`**, **`OFF`**, **`SWP`** auto-sweep)<br>• **`VXX`**: Headphone volume (**0 to 100%**) |
| **1× Short Click** | Cycles active selection: **`GEN` ➔ `HET` ➔ `SIG` ➔ `VOL`** |
| **Hold Knob (≥ 0.7 s)** | Quick-mutes/unmutes test tone (**`SIG: ON` ↔ `SIG: OFF`**) |
| **2× Double-Click** | Exits to Page 1 (`MAIN`) and automatically disables the test generator |

---

## 3. Overnight Autonomous Monitoring (`AUTO-PAM`)

1. Set your **`Route`** (`L1`, `R1`, or `MX`) and check your room's idle **`Ultra`** noise floor in Serial Monitor (`115200` baud) — e.g. if idle reads `~2800`, set **`Thresh`** safely above it (e.g. `4500`–`6000`).
2. Hold **`BOOT`** (`≥ 0.7 s`) or toggle **`AutoPM: ON`** in `SETTINGS`. Badge changes to **`AUTO`**.
3. **How Triggering Works:**
   * A lightweight 192 kHz high-frequency differentiator (`|hp[n] - hp[n-1]|`) ignores low-frequency wind, crickets, and voices while reacting sharply to ultrasound (`>15 kHz`).
   * When `Ultra > Thresh`, badge flashes **`TRIG`**.
   * The ESP32 waits **2.0 seconds** (`2000 ms`) while still recording into the 5-second PSRAM ring buffer, then writes a full **5-second `.WAV`** containing **3 s pre-trigger + 2 s post-trigger**!
   * Enforces a **3.0-second** (`3000 ms`) re-arm cooldown before listening for the next call.
4. **OLED Auto-Sleep:** After **60 seconds** without touching the knob or `BOOT`, the OLED powers off (`SSD1306_DISPLAYOFF`) to save power overnight and wakes instantly when you touch the knob.

---

## 4. Wi-Fi Phone Companion (`192.168.4.1`)

1. Open **Page 4 (`SETTINGS`)** ➔ click **`WiFiAP`** to turn it **`ON` (`192.168.4.1`)**.
2. On your phone or laptop, connect to Wi-Fi network **`Esperdyne-BatNode`** *(open network, no password)*.
3. Open a browser to **`http://192.168.4.1`**:
   * View live auto-updating **Peak Ultrasound (`kHz`)**, **VU %**, **ADC Sample Rate**, and **AUTO/MANUAL** state.
   * Tap **`Save 5s WAV`** or **`Toggle AUTO`** remotely.
   * **Download** or **Delete** any `.WAV` file directly off the MicroSD card without opening your enclosure!
4. Turn **`WiFiAP: OFF`** when done to conserve battery and minimize RF noise.

---

## 5. Hardware Pinout & Board Settings Reference

| Subsystem | Module Pin | ESP32-S3 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **Power** | `VCC` / `3V3` | **`3V3`** | Never power 3.3V SD/OLED/Mics from 5V |
| | `GND` | **`GND`** | Common ground rail |
| **0.91" OLED** | `SDA` / `SCL` | **`GPIO 8`** / **`GPIO 9`** | I2C Address `0x3C` (or `0x3D`) |
| **EC11 Encoder** | `S1 (CLK)` / `S2 (DT)` / `KEY (SW)` | **`GPIO 6`** / **`GPIO 5`** / **`GPIO 4`** | Connect module `5V/+` pin to **`3V3`**! |
| **Built-in Button** | `BOOT` | **`GPIO 0`** | Onboard ESP32-S3 tactile button |
| **MicroSD (SPI)** | `CS` / `MO` / `SCK` / `MI` | **`GPIO 10`** / **`11`** / **`12`** / **`13`** | Dedicated 3.3V module |
| **PCM5102A DAC** | `BCK` / `DIN` / `LCK` | **`GPIO 15`** / **`16`** / **`17`** | Tie `SCK, FLT, DEMP, FMT` ➔ `GND`; `XSMT` ➔ `3V3` |
| **WM8782 ADC** | `BICK` / `DATA` / `LRCK` | **`GPIO 39`** / **`40`** / **`41`** | Jumper `Pin 7 (MCLK)` to `Pin 8 (24.576M)`; DIP: `Master` & `24bit` |
| **Signal Gen (Test)** | `OUT` (Single Wire Loop) | **`GPIO 18`** | Hardware PWM (35–100 kHz) for direct line-level ADC loopback testing |

### Arduino IDE / CLI Compile Settings
* **Board:** `ESP32S3 Dev Module`
* **PSRAM:** `OPI PSRAM`
* **Flash Size:** `16MB (128Mb)`
* **Partition Scheme:** `16M Flash (3MB APP/9.9MB FATFS)`
* **Flash Mode:** `QIO 80MHz`
* **USB CDC On Boot:** `Enabled`
* **Upload Port:** Use the **`COM` (CH343 UART)** USB-C port (`115200` baud Serial Monitor)

### Terminal Upload Command (PowerShell)
```powershell
& "C:\Program Files (x86)\Arduino\arduino_debug.exe" --upload --port COM8 --board "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,FlashMode=qio,UploadSpeed=921600" "c:\Users\sansh\Downloads\EsperdyneBATkonnect\Esperdyne_Full_Firmware\Esperdyne_Full_Firmware.ino"
```
