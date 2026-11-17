# AtiDhwani Bat PAM Motherboard Hardware (v3.1)

Welcome to the hardware repository for the **AtiDhwani Bat PAM Node Carrier Motherboard (Revision v3.1)**. This carrier board integrates all ultrasonic acoustic front-ends, high-speed digital audio converters, power management, storage, and user interface modules onto a single 2-layer PCB designed in KiCad 10.

---

## 1. PCB Specifications

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **Dimensions** | 120.0 mm × 106.0 mm (4.72" × 4.17") | Fits compact weather-sealed IP65/IP67 field enclosures |
| **Layer Count** | 2 Layers (Top & Bottom Copper) | Standard, low-cost fabrication stackup |
| **Board Thickness** | 1.6 mm standard FR-4 | High mechanical rigidity for field deployment |
| **Copper Weight** | 1 oz / 35 µm outer layers | Optimized for power distribution and low-impedance grounds |
| **Surface Finish** | HASL Lead-Free or ENIG | ENIG recommended for MEMS microphone acoustic port reflow |
| **Minimum Trace/Space** | 0.25 mm / 0.25 mm (10 mil / 10 mil) | Standard fab house capability (JLCPCB / PCBWay) |
| **Minimum Drill** | 0.3 mm (via) / 0.8 mm (component pins) | Fabricator compliant |
| **Solder Mask** | Dark Green | High contrast for white silkscreen inspection |
| **Silkscreen** | White | Full pin labels, jumper notes, and component reference designators |
| **Mounting Holes** | 4 × M3 (3.2 mm drill, 6.0 mm clearance) | Located at PCB corners with 5.0 mm edge inset |

---

## 2. Directory Structure

```text
hardware/
├── kicad/                               # Native KiCad 10 project files
│   ├── Esperdyne_Motherboard.kicad_pro  # KiCad Project File
│   ├── Esperdyne_Motherboard.kicad_sch  # Complete Hierarchical Schematic
│   ├── Esperdyne_Motherboard.kicad_pcb  # Fully Routed PCB Layout
│   ├── Esperdyne_Motherboard.kicad_dru  # KiCad Design Rules
│   ├── Esperdyne_Motherboard.kicad_prl  # Project Local Settings
│   ├── sym-lib-table                    # Symbol Library Table
│   └── fp-lib-table                     # Footprint Library Table
├── gerbers/                             # Production fabrication outputs
│   ├── Esperdyne_Motherboard_Gerbers_v3.1.zip # Ready-to-upload JLCPCB/PCBWay Zip
│   ├── *.gbr                            # Gerber layers (F_Cu, B_Cu, F_Mask, B_Mask, Edge_Cuts, etc.)
│   └── *.drl                            # Drill files (Excellon NC format)
├── schematic/                           # Production schematics & prints
│   ├── Esperdyne_Schematic_v3.1.pdf    # Vector PDF schematic with pinout callouts
│   └── schematic_render_v3.1.png        # High-res graphical schematic overview
└── bom/                                 # Bill of Materials
    ├── BOM.md                           # Human-readable markdown BOM with component suppliers
    └── BOM.csv                          # CSV export for automated purchasing / distributor cart import
```

---

## 3. Subsystem Breakdown

### Acoustic Bay (Left Side)
* **MEMS Transducers (`U2`, `U3`):**
  * Knowles SPV0142LR5H-1 wideband acoustic sensor ($10\text{ kHz} - 80+\text{ kHz}$).
  * Infineon IM70A135 ultra-high SNR ($70\text{ dB(A)}$) low-distortion sensor.
* **Acoustic LDO Regulator (`U4`):**
  * AP2127K-2.8V ultra-low-noise linear regulator isolated from digital switching ripple.
* **Microphone Output Header (`J_MIC_OUT`):**
  * 1×03 2.54mm pin header for routing Left/Right AC-coupled analog signals into external RCA inputs of the ADC.

### Audio Conversion & Processing
* **24-Bit Stereo ADC Socket (`J_ADC1`):**
  * 1×06 female header accepting the Wolfson WM8782 Master-Mode 192 kHz audio ADC breakout.
* **32-Bit Stereo DAC Socket (`J_DAC1` & `J_DAC2` - v3.1 Layout):**
  * Dual-header socket configuration matching the purple PCM5102A breakout:
    * `J_DAC1`: 1×06 horizontal female header along the bottom edge (`rot = 90`).
    * `J_DAC2`: 1×09 vertical female header along the left edge (`rot = 180`).
    * Hardware configuration jumpers: `H1L=GND` (FLT), `H2L=GND` (DEMP), `H3L=3.3V` (XSMT soft un-mute), `H4L=GND` (FMT standard I2S).
    * Integrated 3.5mm stereo headphone jack oriented towards the upper-right board perimeter.
* **Microcontroller Sockets (`J_ESP_L1`, `J_ESP_R1`):**
  * Dual 1×22 2.54mm female sockets accepting the ESP32-S3 DevKitC-1 (N16R8 / N8R8).
  * Accompanied by duplicate 1×22 breakout headers (`J_ESP_L2`, `J_ESP_R2`) for full oscilloscope and logic analyzer probing.

### Power & Telemetry Bay (Lower Left)
* **TP4056 Li-Ion Charger Socket (`J_TP4056_IN` & `J_TP4056_OUT`):**
  * 4-pin corner-stand layout for plug-in TP4056 USB-C charging module.
* **XL3608 5V Step-Up Boost Socket (`J_BOOST_IN` & `J_BOOST_OUT`):**
  * 4-pin rectangular header layout supporting the micro boost module.
* **Power Control & Battery In:**
  * `J_BAT_IN`: 2-pin JST-PH / 2.54mm header for 3.7V 18650/LiPo cell.
  * `SW_PWR1`: 1×03 SPDT slide switch for hard power disconnection.
  * Precision 1:2 battery voltage divider ($100\text{ k}\Omega / 100\text{ k}\Omega \pm 1\%$) with a $2.2\text{ nF}$ ceramic filter feeding GPIO2 ADC1.

### User Interface & Storage Bay (Right Side)
* **MicroSD Push-Push Socket (`J_SD1`):**
  * 1×09 female socket oriented with the card insertion slot facing outwards to the right PCB edge.
* **0.91" I2C OLED Display Socket (`J_OLED1` - v3.0 Layout):**
  * 1×04 vertical socket (`rot = 0`) at $(x=174.0, y=111.0)$ mm, holding the screen completely inside the PCB courtyard boundary.
* **EC11 Rotary Encoder (`SW_ENC1`):**
  * 5-pin footprint with integrated push-to-click button for rapid heterodyne tuning and menu navigation.
* **Field Buttons (`SW1` - `SW4`):**
  * Four tactile 6×6 mm momentary switches for Record, Display Mode, Frequency UP, and Frequency DOWN.

---

## 4. Fabrication Guide

To order boards from JLCPCB, PCBWay, or any standard PCB manufacturer:
1. Download the pre-built zip file:
   `hardware/gerbers/Esperdyne_Motherboard_Gerbers_v3.1.zip`
2. Upload the `.zip` archive to your fabricator's online instant quote tool.
3. Select standard options:
   * **Base Material:** FR-4 (TG 130-140)
   * **Dimensions:** 120 mm × 106 mm
   * **Layers:** 2
   * **Thickness:** 1.6 mm
   * **Copper Weight:** 1 oz
   * **Surface Finish:** Lead-Free HASL (or ENIG for easier MEMS soldering)
   * **Solder Mask:** Green (or your preferred color)
   * **Silkscreen:** White
4. KiCad Design Rule Check (DRC) report status: **0 Errors, 0 Warnings, 0 Courtyard Overlaps**.
