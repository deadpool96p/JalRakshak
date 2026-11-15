# 🔬 JalRakshak: 10-Minute Bench Sensor Calibration Guide

This guide provides practical step-by-step bench calibration instructions for the **JalRakshak** node using common laboratory buffers or accessible household standards.

---

## 📋 Required Bench Reference Solutions

| Sensor | Reference Standards | Target Physical Value | Expected ESP32 Voltage |
| :--- | :--- | :--- | :--- |
| **DS18B20** | Room Temperature Water / Ice Slurry | `25.0°C` / `0.0°C` | Digital 1-Wire Data (`GPIO 4`) |
| **DT0530 TDS** | Pure RO / Distilled Water | `0 – 10 ppm` | `0.00 – 0.05 V` (`GPIO 34`) |
| **DT0530 TDS** | Table Salt ($0.5\text{ g}$ NaCl in $1\text{ L}$ water) | $\approx 500\text{ ppm}$ | $\approx 1.25\text{ V}$ (`GPIO 34`) |
| **TS-300B Turbidity** | Clear Tap / RO Water | $< 1.0\text{ NTU}$ | $\ge 3.80\text{ V}$ (`VP / GPIO 36`) |
| **TS-300B Turbidity** | Diluted Milk ($1\text{ drop}$ in $500\text{ mL}$ water) | $\approx 25\text{ – }50\text{ NTU}$ | $\approx 2.70\text{ – }2.90\text{ V}$ (`VP / GPIO 36`) |
| **PH-4502C pH** | Neutral Buffer / Tap Water (pH 7.00) | `7.00` | $\approx 2.50\text{ V}$ (`GPIO 35`) |
| **PH-4502C pH** | Household White Vinegar / pH 4.01 Buffer | $\approx 2.80\text{ – }4.01$ | $\approx 3.00\text{ – }3.20\text{ V}$ (`GPIO 35`) |

---

## ⏱️ Step-by-Step 10-Minute Bench Routine

### Step 1: Open the Hardware Diagnostics Screen (`VIEW: RAW`)
1. Boot the JalRakshak node.
2. Tap the bottom **`VIEW`** button to cycle to **`VW: RAW`** (`VIEW_RAW_DIAGNOSTICS`).
3. You will see live ADC voltages for:
   - `TDS Sensor (GPIO 34)`
   - `Turbidity TS-300B (VP/36)`
   - `pH Probe (GPIO 35)`
   - `DS18B20 Temp (GPIO 4)`

---

### Step 2: TDS Sensor Zero Calibration (2 Minutes)
1. Submerge the TDS probe into a cup of **pure distilled or RO water**.
2. Tap the bottom **`CAL/ZERO`** button once.
3. The display will show the green confirmation banner:
   ```
   [ BASELINE SAVED! ]
   Offsets Stored: TDS=0 | Turb=0
   ```
4. Verify that the displayed TDS reading drops to `0 ppm`.
5. *(Optional verification)*: Add a pinch ($0.5\text{ g}$) of table salt to the cup. Stir well. The TDS reading should jump to $450\text{ – }550\text{ ppm}$ and trigger the amber/yellow border.

---

### Step 3: TS-300B Optical Turbidity Alignment (3 Minutes)
1. Ensure the TS-300B sensor module is powered with **5V** and its analog `AOUT` pin is wired to **`VP` (`GPIO 36`)**.
2. Submerge the sensor head in clear tap water:
   - Check the voltage on screen in `VW: RAW`. It should read $\ge 3.80\text{ V}$.
   - If the voltage is lower than $3.5\text{ V}$ in crystal-clear water, adjust the small blue potentiometer on the TS-300B adapter board until `V_raw` reads between **$3.9\text{ V}$ and $4.1\text{ V}$**.
3. Now add 1 single drop of milk to the glass and stir:
   - The turbidity voltage will drop below $3.0\text{ V}$.
   - On the main dashboard, the reading will climb to $> 20\text{ NTU}$ and status will show `Turbid NTU` with red alert.

---

### Step 4: PH-4502C Hardware Calibration (3 Minutes)
1. Short the BNC central pin to the outer shell (or submerge in a **pH 7.00** buffer solution).
2. Look at the voltage in `VW: RAW` for `pH Probe (GPIO 35)`.
3. Adjust the multi-turn potentiometer on the PH-4502C module closest to the BNC connector until the multimeter / screen voltage reads exactly **$2.50\text{ V}$**.
4. Remove the short and insert the probe into **household white vinegar** ($\text{pH } \approx 2.8\text{ – }3.0$) or a **pH 4.01 buffer sachet**:
   - The voltage should rise to $\approx 3.05\text{ V}$.
   - The mapped pH on the dashboard will display `3.00 – 4.00` in bright red (`Acidic Hazard`).

---

### Step 5: Touchscreen Alignment Recalibration (2 Minutes)
If touches are slightly misaligned on the 2.8" panel:
1. In `VW: RAW`, tap the red row: **`RECAL TOUCH (Tap to Calibrate)`** (or hold `CAL/ZERO` for 2 seconds).
2. The screen clears to black and shows 4 corner targets.
3. Use a stylus or finger to tap each target precisely.
4. The calibration array is automatically saved to ESP32 Flash NVS (`Preferences`), surviving all subsequent reboots!

---

## 📊 Summary of Calibration Coefficients in Code
All validated equations are centralized in:
- `data/sensor_calibration_specs.json`: Machine-readable specification.
- `src/calibration_curves.h`: C++ header linked directly into the firmware.
- `src/water_model.h`: Retrained TinyML weights matching normalized sensor distributions.
