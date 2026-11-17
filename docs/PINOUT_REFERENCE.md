# Master Pinout Reference — AtiDhwani Bat PAM Node Carrier v3.1

---

## 1. ESP32-S3 Primary & Breakout Header Sockets

Both the Left (`J_ESP_L1` / `J_ESP_L2`) and Right (`J_ESP_R1` / `J_ESP_R2`) DevKitC-1 header sockets are duplicated in parallel on the carrier motherboard at standard 2.54mm pitch, providing 100% test-point and patch access to all 44 pins.

### Left Header (`J_ESP_L1` & `J_ESP_L2`)

| Pin | Physical Pin | Net Name | Primary Subsystem | Function / Description |
| :---: | :---: | :--- | :--- | :--- |
| 1 | 3V3 | `+3.3V` | Power | Primary 3.3V digital supply rail |
| 2 | 3V3 | `+3.3V` | Power | Primary 3.3V digital supply rail |
| 3 | EN | `EN` | Reset | Active-low chip reset / enable |
| 4 | IO4 | `IO4` | User Interface | EC11 Rotary Encoder push button switch (`SW`) |
| 5 | IO5 | `IO5` | User Interface | EC11 Rotary Encoder direction signal (`DT`) |
| 6 | IO6 | `IO6` | User Interface | EC11 Rotary Encoder clock signal (`CLK`) |
| 7 | IO7 | `IO7` | User Interface | Tactile Pushbutton `SW1` (Record Toggle, Active-Low) |
| 8 | IO15 | `IO15` | Audio DAC | PCM5102A I2S Bit Clock (`BCK`) |
| 9 | IO16 | `IO16` | Audio DAC | PCM5102A I2S Serial Data In (`DIN`) |
| 10 | IO17 | `IO17` | Audio DAC | PCM5102A I2S Word Select / Left-Right Clock (`LCK`) |
| 11 | IO18 | `IO18` | Expansion | General Purpose I/O |
| 12 | IO8 | `IO8` | Display | 0.91" I2C OLED Serial Data (`SDA`) |
| 13 | IO3 | `IO3` | User Interface | Tactile Pushbutton `SW2` (Display Mode Toggle, Active-Low) |
| 14 | IO46 | `IO46` | Expansion | General Purpose I/O |
| 15 | IO9 | `IO9` | Display | 0.91" I2C OLED Serial Clock (`SCK` / `SCL`) |
| 16 | IO10 | `IO10` | Storage | MicroSD Card Chip Select (`CS`) |
| 17 | IO11 | `IO11` | Storage | MicroSD Card SPI MOSI (`MO`) |
| 18 | IO12 | `IO12` | Storage | MicroSD Card SPI Clock (`SCK`) |
| 19 | IO13 | `IO13` | Storage | MicroSD Card SPI MISO (`MI`) |
| 20 | IO14 | `IO14` | Expansion | General Purpose I/O |
| 21 | 5V | `VBUS` | Power | 5V System Rail (from XL3608 boost converter) |
| 22 | GND | `GND` | Ground | Primary System Ground |

---

### Right Header (`J_ESP_R1` & `J_ESP_R2`)

| Pin | Physical Pin | Net Name | Primary Subsystem | Function / Description |
| :---: | :---: | :--- | :--- | :--- |
| 1 | GND | `GND` | Ground | Primary System Ground |
| 2 | TXD0 | `TXD0` | Serial UART | Primary Serial UART Transmit (USB-UART Bridge) |
| 3 | RXD0 | `RXD0` | Serial UART | Primary Serial UART Receive (USB-UART Bridge) |
| 4 | IO1 | `IO1` | User Interface | Tactile Pushbutton `SW3` (Frequency UP, Active-Low) |
| 5 | IO2 | `IO2` | Telemetry | Battery Voltage Gauge (1:2 Divider with 2.2nF Filter) |
| 6 | IO42 | `IO42` | Expansion | General Purpose I/O |
| 7 | IO41 | `IO41` | Audio ADC | WM8782 I2S Left-Right Word Clock (`LRCK` Input) |
| 8 | IO40 | `IO40` | Audio ADC | WM8782 I2S Serial Data (`DATA` Input) |
| 9 | IO39 | `IO39` | Audio ADC | WM8782 I2S Bit Clock (`BICK` Input) |
| 10 | IO38 | `IO38` | Expansion | General Purpose I/O |
| 11 | IO37 | `IO37` | Expansion | General Purpose I/O |
| 12 | IO36 | `IO36` | Expansion | General Purpose I/O |
| 13 | IO35 | `IO35` | Expansion | General Purpose I/O |
| 14 | IO0 | `IO0` | Boot | Strapping / Boot button |
| 15 | IO45 | `IO45` | Expansion | General Purpose I/O |
| 16 | IO48 | `IO48` | Expansion | General Purpose I/O |
| 17 | IO47 | `IO47` | Expansion | General Purpose I/O |
| 18 | IO21 | `IO21` | User Interface | Tactile Pushbutton `SW4` (Frequency DOWN, Active-Low) |
| 19 | IO20 | `IO20` | USB | Native USB D- (`USB_DM`) |
| 20 | IO19 | `IO19` | USB | Native USB D+ (`USB_DP`) |
| 21 | GND | `GND` | Ground | Primary System Ground |
| 22 | GND | `GND` | Ground | Primary System Ground |

---

## 2. Right Bay Peripheral Pinouts (v3.1 Layout)

### 0.91" I2C OLED Display (`J_OLED1`)
* **Socket:** 1x04 Vertical Female Header at `x = 174.0 mm, y = 111.0 mm` (`rot = 0`)
* **Dimensions:** 38.0mm × 12.0mm module body; 30.0mm × 11.4mm glass viewport (100% enclosed within PCB perimeter)
* **Pin Mapping (Top to Bottom):**
  * `Pin 1 (y=111.00)`: `SDA` $\rightarrow$ `GPIO 8`
  * `Pin 2 (y=113.54)`: `SCK / SCL` $\rightarrow$ `GPIO 9`
  * `Pin 3 (y=116.08)`: `VCC` $\rightarrow$ `+3.3V`
  * `Pin 4 (y=118.62)`: `GND` $\rightarrow$ `GND`

---

### MicroSD SPI Socket (`J_SPI1`)
* **Socket:** 1x09 Vertical Female Header at `x = 184.0 mm, y = 124.0 mm` (`rot = 0`)
* **Orientation:** Push-push card insertion slot faces directly outward to the right edge (`x1 = 215.0 mm`)
* **Pin Mapping (Top to Bottom):**
  * `Pin 1 (y=124.00)`: `3V3` $\rightarrow$ `+3.3V`
  * `Pin 2 (y=126.54)`: `D1` $\rightarrow$ NC
  * `Pin 3 (y=129.08)`: `SCK` $\rightarrow$ `GPIO 12`
  * `Pin 4 (y=131.62)`: `MO` $\rightarrow$ `GPIO 11` (MOSI)
  * `Pin 5 (y=134.16)`: `MI` $\rightarrow$ `GPIO 13` (MISO)
  * `Pin 6 (y=136.70)`: `CS` $\rightarrow$ `GPIO 10`
  * `Pin 7 (y=139.24)`: `CD` $\rightarrow$ NC
  * `Pin 8 (y=141.78)`: `GND` $\rightarrow$ `GND`
  * `Pin 9 (y=144.32)`: `D2` $\rightarrow$ NC

---

### PCM5102A I2S DAC (`J_DAC1` & `J_DAC2`)
* **Bottom Header (`J_DAC1`):** 1x06 Horizontal along bottom row at `y = 178.00 mm`, `x = 184.00` to `196.70 mm` (`rot = 90`)
  * `Pin 1 (x=184.00)`: `SCK` $\rightarrow$ `GND` (**PLL Enabled**)
  * `Pin 2 (x=186.54)`: `BCK` $\rightarrow$ `GPIO 15`
  * `Pin 3 (x=189.08)`: `DIN` $\rightarrow$ `GPIO 16`
  * `Pin 4 (x=191.62)`: `LCK` $\rightarrow$ `GPIO 17`
  * `Pin 5 (x=194.16)`: `GND` $\rightarrow$ `GND`
  * `Pin 6 (x=196.70)`: `VIN` $\rightarrow$ `+3.3V`
* **Left Header (`J_DAC2`):** 1x09 Vertical along left column at `x = 184.00 mm`, running upwards from `y = 172.92` to `152.60 mm` (`rot = 180`)
  * `Pin 1 (y=172.92)`: `FLT` $\rightarrow$ `GND` (Normal latency)
  * `Pin 2 (y=170.38)`: `DEMP` $\rightarrow$ `GND` (De-emphasis OFF)
  * `Pin 3 (y=167.84)`: `XSMT` $\rightarrow$ `+3.3V` (**Soft Un-Mute Active**)
  * `Pin 4 (y=165.30)`: `FMT` $\rightarrow$ `GND` (Standard I2S)
  * `Pin 5 (y=162.76)`: `A3V3` $\rightarrow$ `+3.3V`
  * `Pin 6 (y=160.22)`: `AGND` $\rightarrow$ `GND`
  * `Pin 7 (y=157.68)`: `ROUT` $\rightarrow$ `DAC_ROUT`
  * `Pin 8 (y=155.14)`: `AGND` $\rightarrow$ `GND`
  * `Pin 9 (y=152.60)`: `LROUT` $\rightarrow$ `DAC_LOUT`
* **Headphone Jack Position:** Top-Right of DAC footprint (`x ≈ 213.5 mm, y ≈ 152.0 - 160.0 mm`), facing outward through the right enclosure wall.
