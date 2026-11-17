# Bill of Materials (BOM) — AtiDhwani Bat PAM Node Carrier Motherboard

**Hardware Revision:** `v3.1`  
**Board Dimensions:** `115.0 mm × 98.0 mm`  
**Layers:** 2 Layers (1 oz Copper, HASL Lead-Free / ENIG recommended)  

---

## Complete Component List

| Item | Reference | Qty | Value / Part | Footprint | Subsystem | Description |
| :--- | :--- | :---: | :--- | :--- | :--- | :--- |
| 1 | `U1` | 1 | ESP32-S3-DevKitC-1 | Socket (2x 1x22 2.54mm) | Core MCU | ESP32-S3 Dual-Core Xtensa LX7 (8MB Flash / 8MB PSRAM) |
| 2 | `J_ESP_L1`, `J_ESP_R1` | 2 | 1x22 Pin Socket | PinSocket_1x22_P2.54mm_Vertical | Core MCU | Primary DevKitC-1 receiver headers |
| 3 | `J_ESP_L2`, `J_ESP_R2` | 2 | 1x22 Pin Socket | PinSocket_1x22_P2.54mm_Vertical | Expansion | Parallel breakout sockets for 100% pin access |
| 4 | `U4` | 1 | AP2127K-2.8 | SOT-23-5 | Analog Power | Ultra-low-noise 2.8V 300mA LDO regulator |
| 5 | `C1` | 1 | 1µF 16V X7R | C_0805_2012Metric | Analog Power | LDO Input decoupling capacitor |
| 6 | `C2` | 1 | 2.2µF 10V X7R | C_0805_2012Metric | Analog Power | LDO Output stabilizing capacitor |
| 7 | `MIC_KNW` | 1 | SPV0142LR5H-1 | 4-Pad Bottom Port (2.75x1.85mm) | Ultrasonic Audio | Knowles ultrasonic analog MEMS microphone (10–80 kHz) |
| 8 | `MIC_INF` | 1 | IM70A135 | 5-Pad Bottom Port (4.00x3.00mm) | Ultrasonic Audio | Infineon ultra-high SNR analog MEMS microphone |
| 9 | `C_KNW`, `C_INF` | 2 | 2.2µF 16V X7R | C_0805_2012Metric | Ultrasonic Audio | AC-coupling capacitors on mic analog outputs |
| 10 | `J_MIC_OUT` | 1 | 1x03 Header | PinHeader_1x03_P2.54mm_Vertical | Audio Patch | Analog output patch header (MIC1, MIC2, GND) to ADC RCA |
| 11 | `J_ADC1` | 1 | 1x06 Pin Socket | PinSocket_1x06_P2.54mm_Vertical | Audio ADC | 6-pin I2S digital audio cable socket to WM8782 board |
| 12 | `MOD_ADC` | 1 | WM8782 Module | External Daughterboard | Audio ADC | 24-bit 192 kHz stereo audio ADC breakout |
| 13 | `J_TP4056` | 1 | TP4056 Type-C | Modular Daughterboard (4 corners) | Power | 5V 1A Lithium battery charging module with Type-C |
| 14 | `J_BATT` | 1 | 1x02 Pin Header | PinHeader_1x02_P2.54mm_Vertical | Power | 3.7V Li-Ion / Li-Po battery connection |
| 15 | `SW_PWR` | 1 | SPDT Slide Switch | PinHeader_1x03_P2.54mm_Vertical | Power | Main system battery power toggle switch |
| 16 | `J_BOOST` | 1 | XL3608 Mini | 4-Pin Socket (Rectangular layout) | Power | High-efficiency 5V DC-DC boost converter module |
| 17 | `R_DIV1`, `R_DIV2` | 2 | 100kΩ 1% | R_0805_2012Metric | Power Telemetry | 1:2 precision battery voltage divider to ADC IO2 |
| 18 | `C_FILT` | 1 | 2.2nF 50V | C_0805_2012Metric | Power Telemetry | High-frequency noise filter on battery ADC line |
| 19 | `J_OLED1` | 1 | 1x04 Pin Socket | PinSocket_1x04_P2.54mm_Vertical | Display | Vertical socket for 0.91" I2C OLED display |
| 20 | `DISP1` | 1 | SSD1306 0.91" | 128x32 I2C OLED Module | Display | Monochrome graphical user interface display |
| 21 | `J_SPI1` | 1 | 1x09 Pin Socket | PinSocket_1x09_P2.54mm_Vertical | Storage | Outward-facing vertical socket for 9-pin MicroSD SPI board |
| 22 | `MOD_SD` | 1 | PCBCupid MicroSD | 9-Pin SPI Daughterboard | Storage | Push-push MicroSD card module for high-speed logging |
| 23 | `J_DAC1` | 1 | 1x06 Pin Socket | PinSocket_1x06_P2.54mm_Vertical | Audio Output | Horizontal bottom socket for GY-PCM5102 I2S pins |
| 24 | `J_DAC2` | 1 | 1x09 Pin Socket | PinSocket_1x09_P2.54mm_Vertical | Audio Output | Vertical left socket for GY-PCM5102 config & analog pins |
| 25 | `MOD_DAC` | 1 | GY-PCM5102A | Dual-Header Daughterboard | Audio Output | 32-bit I2S DAC with integrated 3.5mm stereo headphone jack |
| 26 | `J_ENC1` | 1 | 1x05 Pin Socket | PinSocket_1x05_P2.54mm_Vertical | UI Controls | Socket for EC11 rotary encoder with integrated push switch |
| 27 | `SW1`..`SW4` | 4 | Tactile Switch | SW_Push_SPST_NO_6x6mm | UI Controls | 6mm pushbuttons (SW1: REC, SW2: DISP, SW3: UP, SW4: DOWN) |
| 28 | `D1` | 1 | LED Green/Blue | LED_0805_2012Metric | Indicator | Power status indicator LED on VBUS |
| 29 | `R_LED` | 1 | 1kΩ 5% | R_0805_2012Metric | Indicator | Current limiting resistor for power LED |
| 30 | `H1`..`H4` | 4 | M3 Mounting Hole | MountingHole_3.2mm_M3 | Mechanical | 3.2mm drill mounting holes with 6mm clearance keepout |
