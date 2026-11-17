# Esperdyne Firmware — DSP Roadmap & Parked Improvements

This document tracks planned DSP improvements parked for the final polishing stage.

* Full details & hardware notes: [`../Esperdyne_Future_Improvements.md`](../Esperdyne_Future_Improvements.md)

---

### Parked DSP Upgrades for Final Polish

#### 1. 2-Pole Butterworth Low-Pass Filter (5.5 kHz)
* **Goal:** Replace 1st-order decimation IIR (`0.55 * (in - out)`) with a 2nd-order Direct Form II Transposed biquad filter.
* **Why:** Provides >24 dB attenuation above 6 kHz, eliminating intermodulation screech from high-frequency beat tones (15–21 kHz) and suppressing square-wave test harmonics.
* **Coefficients (48 kHz DAC sample rate, fc = 5.5 kHz):**
  - $b_0 = 0.0827, b_1 = 0.1654, b_2 = 0.0827$
  - $a_1 = -1.0478, a_2 = 0.3786$

#### 2. Soft-Knee Polynomial Limiter
* **Goal:** Replace hard `constrain(-32700, 32700)` with a smooth hyperbolic tangent / polynomial soft saturator above 24,000 counts.
* **Why:** Analog-style tube warmth on loud bat passes instead of hard digital clipping.

#### 3. Logarithmic Volume Taper (Done in v2.1)
* **Status:** Implemented in `Esperdyne_Full_Firmware.ino` as `VolCrv: LOG (EARS)` in Settings menu.
* **Formula:** $\text{vol} = (\text{VOL} / 100)^2$. Default boot volume: 30%.

