# 3D Enclosure Mechanical Integration Guide — AtiDhwani Bat PAM Node

The AtiDhwani carrier motherboard (`v3.1`) measures **115.0 mm × 98.0 mm** and has been engineered specifically for integration into a handheld, field-deployable 3D-printed enclosure.

---

## 1. PCB Mounting Dimensions

* **Mounting Holes:** 4x M3 mounting holes (`3.2 mm` drill diameter)
* **Corner Inset:** $5.0\text{ mm}$ from each board edge:
  * **Top-Left (H1):** `(105.0 mm, 105.0 mm)`
  * **Top-Right (H2):** `(210.0 mm, 105.0 mm)`
  * **Bottom-Left (H3):** `(105.0 mm, 193.0 mm)`
  * **Bottom-Right (H4):** `(210.0 mm, 193.0 mm)`
* **Center-to-Center Spacing:**
  * Horizontal hole-to-hole span: **105.0 mm**
  * Vertical hole-to-hole span: **88.0 mm**
* **Standoff Height:** Recommended internal standoff height is **6.0 mm – 8.0 mm** using M3 brass heat-set inserts (e.g., Ruthex M3x5.7).

---

## 2. Right Enclosure Wall Port Cutouts

The right wall of the enclosure (`x = 215.0 mm`) features all primary user-accessible ports:

| Port | Subsystem | Vertical Position (Y) | Recommended Cutout Dimensions | Description |
| :--- | :--- | :---: | :---: | :--- |
| **3.5mm Headphone Jack** | PCM5102A DAC | `y = 156.0 mm` | $7.0\text{ mm}$ diameter circular hole | Direct headphone plug access |
| **MicroSD Card Slot** | MicroSD SPI Breakout | `y = 135.0 mm` | $14.0\text{ mm} \times 3.0\text{ mm}$ slot | Push-push card insertion & ejection |

---

## 3. Left Enclosure Wall Port Cutouts

The left wall of the enclosure (`x = 100.0 mm`) houses the power management controls:

| Control | Subsystem | Vertical Position (Y) | Recommended Cutout Dimensions | Description |
| :--- | :--- | :---: | :---: | :--- |
| **USB-C Charging Receptacle** | TP4056 Module | `y = 147.0 mm` | $10.0\text{ mm} \times 4.5\text{ mm}$ rounded slot | 5V 1A Li-Ion battery charging |
| **Power Slide Switch** | SPDT Toggle | `y = 161.0 mm` | $9.0\text{ mm} \times 4.0\text{ mm}$ slot | Main battery power switch actuator |

---

## 4. Top Case / Faceplate Cutouts

The top faceplate of the enclosure houses the display, rotary encoder, pushbuttons, and acoustic inlet ports:

1. **OLED Display Window:**
   * Center: `x = 193.0 mm, y = 114.8 mm`
   * Cutout Size: $32.0\text{ mm} \times 12.0\text{ mm}$
   * Recommended: $1.0\text{ mm}$ thick transparent acrylic window panel.
2. **Rotary Encoder Shaft:**
   * Center: `x = 190.0 mm, y = 185.0 mm`
   * Cutout Size: $7.5\text{ mm}$ circular opening for standard $6.0\text{ mm}$ D-shaft aluminum knob.
3. **Tactile Pushbuttons:**
   * `SW1` (REC): `x = 145.0 mm, y = 186.0 mm` ($5.0\text{ mm}$ round opening)
   * `SW2` (DISP): `x = 155.0 mm, y = 186.0 mm` ($5.0\text{ mm}$ round opening)
   * `SW3` (UP): `x = 165.0 mm, y = 186.0 mm` ($5.0\text{ mm}$ round opening)
   * `SW4` (DOWN): `x = 175.0 mm, y = 186.0 mm` ($5.0\text{ mm}$ round opening)
4. **Ultrasonic Acoustic Inlets:**
   * Knowles SPV0142: `x = 113.0 mm, y = 105.0 mm` ($2.5\text{ mm}$ acoustic hole)
   * Infineon IM70A135: `x = 126.0 mm, y = 105.0 mm` ($2.5\text{ mm}$ acoustic hole)
   * Protection: Apply hydrophobic acoustic mesh (Gore-Tex acoustic vent or stainless steel 200-mesh) to block moisture and dust while transmitting ultrasonic frequencies up to $100\text{ kHz}$.
