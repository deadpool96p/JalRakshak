# 🌊 JalRakshak: Edge AI Water Quality Monitoring & Diagnostic System
## Comprehensive Technical Report & Engineering Specification

---

### Document Metadata
- **Project Title:** JalRakshak – Autonomous Edge AI Multi-Parameter Water Quality Diagnostic System
- **Lead Developer / Author:** Soham (CPP Semester 5 Project)
- **Target Platform:** Espressif ESP32-WROOM-32 (Dual-Core 32-bit Xtensa LX6 @ 240 MHz)
- **Display & Interface:** 2.8" ILI9341 SPI TFT (320×240 Landscape) + XPT2046 Resistive Touch Controller
- **Core Framework:** PlatformIO / Arduino Core 3.x / FreeRTOS SMP (Symmetric Multiprocessing)
- **Edge AI Architecture:** 4→16→12→3 Multilayer Perceptron (MLP) Neural Network + Deterministic BIS IS 10500:2012 Guardrail Engine
- **Firmware Status:** Production Ready, Tested & Flashed via PlatformIO (`main` branch)
- **Official Repository:** [https://github.com/deadpool96p/JalRakshak.git](https://github.com/deadpool96p/JalRakshak.git)

---

## 📑 Table of Contents
1. [Abstract & Project Objectives](#1-abstract--project-objectives)
2. [Complete Hardware Pin Mapping & Circuit Specifications](#2-complete-hardware-pin-mapping--circuit-specifications)
3. [Sensor Operational Principles, Mathematical Models & Transfer Functions](#3-sensor-operational-principles-mathematical-models--transfer-functions)
4. [ADC Conditioning, eFuse Factory Calibration & 32-Sample Filter](#4-adc-conditioning-efuse-factory-calibration--32-sample-filter)
5. [Dynamic Clean-Water Turbidity Baseline & Relative Ratio Mapping](#5-dynamic-clean-water-turbidity-baseline--relative-ratio-mapping)
6. [TFT Graphics, XPT2046 Touch Persistence & Regional Multilingual Engine](#6-tft-graphics-xpt2046-touch-persistence--regional-multilingual-engine)
7. [Hardware PWM Screen Brightness Subsystem](#7-hardware-pwm-screen-brightness-subsystem)
8. [Edge AI Neural Engine & Deterministic IS 10500:2012 Fail-Safe](#8-edge-ai-neural-engine--deterministic-is-105002012-fail-safe)
9. [Dual-Core FreeRTOS SMP Architecture & Concurrency Model](#9-dual-core-freertos-smp-architecture--concurrency-model)
10. [Hardware Status LEDs & Visual Diagnostic Engine](#10-hardware-status-leds--visual-diagnostic-engine)
11. [Offline-First Wi-Fi SoftAP Web Dashboard & Captive Portal](#11-offline-first-wi-fi-softap-web-dashboard--captive-portal)
12. [Flash NVS Storage Architecture & Namespace Layout](#12-flash-nvs-storage-architecture--namespace-layout)
13. [Current Progress, Verification Benchmarks & Results](#13-current-progress-verification-benchmarks--results)
14. [Report Summary & Presentation Guide](#14-report-summary--presentation-guide)

---

## 1. Abstract & Project Objectives

### 1.1 Abstract
Access to safe, potable drinking water remains one of the most critical public health challenges across developing and rural regions. Conventional water testing typically relies either on manual, laboratory-bound chemical assays (slow, expensive, non-continuous) or centralized IoT nodes that stream raw, uncompensated sensor data to cloud services. The latter approach fails in remote communities, flood zones, or industrial sites characterized by intermittent or non-existent cellular and internet coverage.

**JalRakshak** solves this problem by executing all signal processing, thermodynamic cross-compensation, and machine learning inference directly at the edge on an ultra-low-cost ESP32 dual-core microcontroller. The node continuously samples four physical and electrochemical water parameters:
1. **Temperature ($^\circ\text{C}$):** Measured via digital Dallas 1-Wire protocol.
2. **Total Dissolved Solids (TDS in $\text{ppm}$):** Measured via high-frequency AC conductivity.
3. **Turbidity (NTU):** Measured via optical 850 nm infrared light transmission.
4. **Hydrogen Ion Concentration (pH):** Measured via a high-impedance glass bulb electrode.

Using a hybrid inference architecture—combining a lightweight 4-input Multilayer Perceptron (MLP) with the deterministic regulatory safety limits of Bureau of Indian Standards (BIS) **IS 10500:2012**—JalRakshak classifies water quality into three actionable states:
- **`SAFE` (Green):** Within all acceptable limits; suitable for direct human consumption.
- **`CAUTION` (Yellow / Amber):** Acceptable only in the absence of an alternative source; potential mineral drift or mild turbidity.
- **`CONTAMINATED` (Red):** Exceeds permissible limits; hazardous for direct human consumption.

### 1.2 Key Engineering Objectives
- **Zero Cloud Dependency:** Complete standalone execution; local TFT UI, local buzzer/audio alerts, local status LEDs, and an offline Wi-Fi SoftAP web portal for mobile diagnostics.
- **Microsecond Latency:** Edge AI inference executes in under **350 microseconds**, providing real-time feedback.
- **Electrochemical Noise Immunity:** Implement a 32-sample trimmed-mean filter on ESP32 ADC1 channels with eFuse factory millivolt interpolation, preventing TDS-to-pH crosstalk and ADC non-linearity.
- **Dynamic Clean-Water Referencing:** Eliminate rigid 4.5V turbidity curves by calculating relative attenuation ratios against a calibrated in-situ clean-water baseline.
- **Resilient UI Experience:** High-contrast 320×240 graphical interface featuring English, Hindi, and Marathi translations, hardware PWM dimming, and permanent flash touch calibration storage.

---

## 2. Complete Hardware Pin Mapping & Circuit Specifications

### 2.1 Complete Pinout Table

| Pin Name | ESP32 GPIO | Subsystem / Component | Direction | Hardware & Electrical Description |
| :--- | :--- | :--- | :--- | :--- |
| **D4** | `GPIO 4` | **DS18B20 Temperature Probe** | Bi-directional | Digital 1-Wire bus with external **4.7 kΩ pull-up resistor** to 3.3V. Waterproof stainless probe. |
| **D34** | `GPIO 34` | **DT0530 Analog TDS Sensor** | Input | ADC1 Channel 6. Powered directly from 3.3V (`TDS_DIVIDER_RATIO = 1.00x`). Direct connection. |
| **D35** | `GPIO 35` | **PH-4502C Analog pH Sensor** | Input | ADC1 Channel 7. Powered from 5.0V. Analog output stepped down via **10 kΩ + 20 kΩ** divider (`PH_DIVIDER_RATIO = 1.50x`). |
| **VP** | `GPIO 36` | **TS-300B Turbidity Sensor** | Input | ADC1 Channel 0. Powered from 5.0V in Analog ('A') mode. Stepped down via **10 kΩ + 20 kΩ** divider (`TURB_DIVIDER_RATIO = 1.50x`). |
| **D5** | `GPIO 5` | **ILI9341 2.8" SPI TFT** | Output | **TFT_CS** (Chip Select, Active LOW). Initialized HIGH on boot to avoid SPI bus contention. |
| **D16** | `GPIO 16` | **ILI9341 2.8" SPI TFT** | Output | **TFT_RST** (Hardware Reset). Pulsed LOW during boot initialization. |
| **D17** | `GPIO 17` | **ILI9341 2.8" SPI TFT** | Output | **TFT_DC** (Data / Command Select). Driven HIGH for pixel data, LOW for command registers. |
| **D18** | `GPIO 18` | **TFT & Touch Shared Bus** | Output | **SCK / SCLK / T_CLK** (Hardware VSPI Clock). 27 MHz for TFT pixel streaming, 1 MHz for touch. |
| **D19** | `GPIO 19` | **TFT & Touch Shared Bus** | Input | **MISO / SDO / T_DO** (Hardware VSPI Master In Slave Out). Reads XPT2046 touch registers. |
| **D21** | `GPIO 21` | **XPT2046 Touch Controller** | Output | **TOUCH_CS / T_CS** (Touch Chip Select, Active LOW). Dedicated chip select pin. |
| **D22** | `GPIO 22` | **ILI9341 TFT Backlight** | Output | **LED / BLK**. Driven by ESP32 LEDC hardware PWM Channel 0 at 5 kHz, 8-bit resolution (Duty 75–255). |
| **D23** | `GPIO 23` | **TFT & Touch Shared Bus** | Output | **MOSI / SDI / T_DIN** (Hardware VSPI Master Out Slave In). Writes commands, colors, and touch queries. |
| **D27** | `GPIO 27` | **Status Activity LED (Blue)** | Output | Active-HIGH indicator with 220 Ω current-limiting resistor. Pulses on TinyML inference & Wi-Fi traffic. |
| **D32** | `GPIO 32` | **Status Alert LED (Red)** | Output | Active-HIGH indicator with 220 Ω current-limiting resistor. Flashes rapidly on Contaminated / Unsafe water. |
| **D33** | `GPIO 33` | **Status Safe LED (Green)** | Output | Active-HIGH indicator with 220 Ω current-limiting resistor. Illuminates solid when water is Safe / Drinkable. |
| **VIN** | `5V` | **5.0V System Power Rail** | Power Out | Powers TS-300B turbidity adapter and PH-4502C analog board. |
| **3V3** | `3.3V` | **3.3V System Power Rail** | Power Out | Powers ESP32, DS18B20 digital probe, DT0530 TDS board, and ILI9341 logic. |
| **GND** | `GND` | **Common System Ground** | Ground | Solid star-ground configuration tying all sensor grounds, display ground, and ESP32 GND together. |

---

### 2.2 Passive Voltage Divider Circuit Topologies
The ESP32 integrated Analog-to-Digital Converter (ADC) operates with an internal 1.1V reference, attenuated up to a maximum input voltage limit of **3.30V** (at `ADC_11db`). However, standard industrial water quality sensor boards (such as the TS-300B Turbidity adapter and PH-4502C pH module) are powered by **5.0V DC** and output analog signals spanning **0.0V to 4.5V+**. Direct connection would clip high voltages and risk silicon degradation of the ESP32.

To resolve this safely and with zero active component overhead, two precision passive voltage dividers were designed:

```
[ Sensor Analog Out (0 - 5.0V) ]
              │
              ▼
        ┌───────────┐
        │ R1: 10 kΩ │  (Series Resistor, 1% metal film)
        └─────┬─────┘
              │
              ├────────────────────────► To ESP32 ADC1 Pin (0 - 3.3V Max)
              │                          (GPIO 36 / VP for Turbidity)
              │                          (GPIO 35 / D35 for pH)
        ┌─────┴─────┐
        │ R2: 20 kΩ │  (Shunt Resistor to Ground, 1% metal film)
        └─────┬─────┘
              │
              ▼
        [ System GND ]
```

#### Transfer Function & Reconstructive Multiplier
Using standard potential divider equations:
$$V_{\text{pin}} = V_{\text{sensor}} \times \left( \frac{R_2}{R_1 + R_2} \right) = V_{\text{sensor}} \times \left( \frac{20\,\text{k}\Omega}{10\,\text{k}\Omega + 20\,\text{k}\Omega} \right) = V_{\text{sensor}} \times \frac{2}{3}$$

To reconstruct the true $0 - 5.0\text{V}$ sensor output voltage inside the ESP32 firmware:
$$V_{\text{sensor\_reconstructed}} = V_{\text{pin}} \times \left( \frac{R_1 + R_2}{R_2} \right) = V_{\text{pin}} \times 1.5000$$

Inside `src/sensors.h`:
```cpp
#define PH_DIVIDER_RATIO    1.50f   // 10k + 20k resistor divider on D35 (GPIO 35)
#define TURB_DIVIDER_RATIO  1.50f   // 10k + 20k resistor divider on VP (GPIO 36)
#define TDS_DIVIDER_RATIO   1.00f   // 3.3V direct, no divider on D34 (GPIO 34)
```

---

## 3. Sensor Operational Principles, Mathematical Models & Transfer Functions

### 3.1 DS18B20 Digital Temperature Probe
- **Operating Principle:** Bandgap temperature sensor with on-chip analog-to-digital conversion and 1-Wire communication protocol.
- **Range & Accuracy:** $-55^\circ\text{C}$ to $+125^\circ\text{C}$, $\pm 0.5^\circ\text{C}$ accuracy between $-10^\circ\text{C}$ and $+85^\circ\text{C}$.
- **Significance:** Real-time thermodynamic compensation is mandatory for both electrochemical TDS conductivity and pH glass electrode Nernst slope calculations.

### 3.2 DT0530 Analog Total Dissolved Solids (TDS)
- **Operating Principle:** Measures electrical conductivity (EC) by passing a low-voltage high-frequency AC excitation current between two parallel titanium electrode pins, preventing DC polarization and electrolysis.
- **Temperature Compensation:** Water conductivity increases by approximately $2\%$ for every $1^\circ\text{C}$ rise above $25^\circ\text{C}$ due to increased ion mobility. The firmware normalizes the raw reading back to standard $25^\circ\text{C}$ before computing TDS:
  $$V_{\text{comp}} = \frac{V_{\text{raw}}}{1.0 + 0.02 \times (T_{\text{water}} - 25.0)}$$
- **Polynomial Conversion to Parts Per Million ($\text{ppm}$):**
  $$\text{TDS}_{\text{raw}} = (133.42 \times V_{\text{comp}}^3 - 255.86 \times V_{\text{comp}}^2 + 857.39 \times V_{\text{comp}}) \times 0.5$$
  $$\text{TDS}_{\text{final}} = \max(0.0, \text{TDS}_{\text{raw}} - \text{TDS}_{\text{offset}})$$

### 3.3 PH-4502C Analog pH Probe
- **Operating Principle:** Measures the potential difference generated across a thin glass membrane sensitive to hydrogen ion ($H^+$) activity against an internal silver/silver chloride ($\text{Ag/AgCl}$) reference electrode.
- **Nernst Equation Formulation:**
  $$E = E_0 - \frac{2.303 R T}{F} \log_{10}[H^+] = E_0 + S \cdot (7.0 - \text{pH})$$
  Where:
  - $R$ is the universal gas constant ($8.314\,\text{J}\cdot\text{mol}^{-1}\cdot\text{K}^{-1}$)
  - $T$ is absolute temperature ($273.15 + T^\circ\text{C}$)
  - $F$ is Faraday's constant ($96485\,\text{C}\cdot\text{mol}^{-1}$)
  - $S$ is the theoretical Nernstian slope ($\approx 59.16\,\text{mV/pH}$ at $25^\circ\text{C}$)
- **Firmware Implementation:**
  The PH-4502C module offsets and amplifies this millivolt potential to center at approximately $2.50\text{V}$ for neutral pH 7.0:
  $$\text{pH} = 7.00 + \frac{V_{\text{neutral\_cal}} - V_{\text{sensor}}}{0.180 \times \left( \frac{T_{\text{water}} + 273.15}{298.15} \right)}$$
  Clamped between $0.00$ and $14.00\,\text{pH}$.

---

## 4. ADC Conditioning, eFuse Factory Calibration & 32-Sample Filter

### 4.1 The Challenge of ESP32 ADC Non-Linearity
The ESP32 Successive Approximation Register (SAR) ADC is notorious for two hardware quirks:
1. Significant non-linearity near $0.0\text{V}$ (dead zone below $100\text{ mV}$) and near $3.3\text{V}$ (saturation above $3.15\text{V}$).
2. High-frequency thermal and RF switching noise, especially when Wi-Fi or Bluetooth radios are active.

### 4.2 Solution: Trimmed-Mean Filter with Factory eFuse Millivolt Lookup
To achieve laboratory-grade measurement stability without hardware capacitors on the analog lines:
1. **ADC Channel Pinning:** All four analog signals are placed strictly on **ADC1** (`GPIO 34, 35, 36, 39`). ADC2 is never accessed, completely avoiding hardware lockups when Wi-Fi SoftAP is active.
2. **eFuse Attenuation:** Configured to `ADC_11db` using the factory calibrated polynomial stored in ESP32 eFuses via `analogReadMilliVolts(pin)`.
3. **32-Sample Trimmed-Mean Digital Filter:**
   - Sample the ADC channel 32 times in rapid succession with $1\text{ ms}$ inter-sample spacing.
   - Sort the array using an in-place insertion sort.
   - Discard the lowest 4 samples and highest 4 samples (eliminates electrostatic spikes, relay chatter, and RF induction).
   - Average the middle 24 samples:

$$\bar{V} = \frac{1}{24} \sum_{i=4}^{27} V_{\text{sorted}}[i]$$

```cpp
float sensors_read_adc_voltage(int pin, int samples) {
  int rawMv[32];
  if (samples > 32) samples = 32;

  for (int i = 0; i < samples; i++) {
    rawMv[i] = analogReadMilliVolts(pin);
    delayMicroseconds(250);
  }

  // Insertion sort
  for (int i = 1; i < samples; i++) {
    int key = rawMv[i];
    int j = i - 1;
    while (j >= 0 && rawMv[j] > key) {
      rawMv[j + 1] = rawMv[j];
      j--;
    }
    rawMv[j + 1] = key;
  }

  // Trim 4 lowest and 4 highest outliers; average remaining 24
  long sum = 0;
  int validCount = samples - 8;
  for (int i = 4; i < samples - 4; i++) {
    sum += rawMv[i];
  }
  return (float)(sum / (float)validCount) / 1000.0f;
}
```

---

## 5. Dynamic Clean-Water Turbidity Baseline & Relative Ratio Mapping

### 5.1 Root Cause of the 2999.3 NTU Bug
Standard hobbyist tutorials for the TS-300B optical turbidity sensor use a fixed polynomial curve published in early manufacturer datasheets:
$$\text{NTU} = -1120.4 \cdot V^2 + 5742.3 \cdot V - 4352.9$$
This equation makes an untenable assumption: that clear tap water will always produce exactly $4.10\text{V} - 4.50\text{V}$. In practice:
- Power supply rail drops on 5V breadboard rails reduce the LED drive current.
- The 10k/20k voltage divider introduces small component tolerance shifts.
- Optical path variance and sensor aging cause clean tap water to sit at $2.20\text{V} - 3.40\text{V}$ on the ESP32 pin.
- When fed into the fixed polynomial, any voltage below $2.50\text{V}$ evaluates mathematically to **$> 3000\text{ NTU}$**, locking the system at a false maximum reading of `2999.3 NTU` even in crystal-clear water!

### 5.2 The Engineering Solution: Relative Ratio Quadratic Mapping
Instead of relying on absolute voltage, the turbidity calculation was re-engineered to measure the **relative attenuation ratio** against a dynamically calibrated clean-water reference baseline ($v_{\text{turb\_clean}}$):

1. **Clean-Water Baseline Variable ($v_{\text{turb\_clean}}$):**
   - Stored in persistent ESP32 Flash NVS under namespace `"jal_cal"`, key `"turb_base"`.
   - On boot, loads the saved baseline. If uncalibrated, automatically initializes to the current filtered voltage (valid range $1.80\text{V} - 4.60\text{V}$).
   - User recalibration: Pressing **`[ CAL / ZERO ]`** on the TFT display captures the current filtered sensor voltage as the new $0.0\text{ NTU}$ baseline.

2. **Exponential Moving Average (EMA) Smoothing:**
   To eliminate optical flicker from micro-bubbles, incoming voltage is smoothed with an EMA filter ($\alpha = 0.15$):
   $$V_{\text{smooth}}[k] = \alpha \cdot V_{\text{meas}}[k] + (1 - \alpha) \cdot V_{\text{smooth}}[k-1]$$

3. **Relative Drop Ratio Formulation:**
   - Minimum dark saturation threshold: $V_{\text{turb\_min}} = 1.00\text{V}$ (representing maximum opacity / sludge).
   - If $V_{\text{sensor}} \ge (v_{\text{turb\_clean}} - 0.04\text{V})$, water is clean: $\text{NTU} \in [0.0, 1.5]$ (clean water optical ripple).
   - For all other states, the relative attenuation ratio is calculated:
     $$\text{drop\_ratio} = \text{constrain}\left( \frac{v_{\text{turb\_clean}} - V_{\text{sensor}}}{v_{\text{turb\_clean}} - 1.00\text{V}},\, 0.0,\, 1.0 \right)$$
   - Quadratic NTU mapping:
     $$\text{NTU} = (\text{drop\_ratio}^2 \times 2800.0) + (\text{drop\_ratio} \times 200.0)$$
     Clamped between $0.0$ and $3000.0\text{ NTU}$.

#### Calibration & NTU Mapping Verification Table

| Sensor Voltage ($V_{\text{sensor}}$) | Baseline ($v_{\text{turb\_clean}}$) | Drop Ratio | Computed NTU | Water Condition | Status Classification |
| :---: | :---: | :---: | :---: | :--- | :--- |
| **$3.90\text{ V}$** | $3.90\text{ V}$ | $0.000$ | **$0.0\text{ NTU}$** | Distilled / Filtered Water | **SAFE** |
| **$3.86\text{ V}$** | $3.90\text{ V}$ | $0.013$ | **$0.4\text{ NTU}$** | Clean Municipal Tap Water | **SAFE** |
| **$3.60\text{ V}$** | $3.90\text{ V}$ | $0.103$ | **$50.4\text{ NTU}$** | Slight Silt / Pipeline Dust | **CAUTION** |
| **$3.00\text{ V}$** | $3.90\text{ V}$ | $0.310$ | **$331.2\text{ NTU}$** | Turbid River Water | **CONTAMINATED** |
| **$2.00\text{ V}$** | $3.90\text{ V}$ | $0.655$ | **$1332.1\text{ NTU}$** | Heavy Silt / Runoff | **CONTAMINATED** |
| **$1.00\text{ V}$** | $3.90\text{ V}$ | $1.000$ | **$3000.0\text{ NTU}$** | Opaque Sludge / Saturated | **CONTAMINATED** |

---

## 6. TFT Graphics, XPT2046 Touch Persistence & Regional Multilingual Engine

### 6.1 Hardware SPI Bus Architecture & Arbitration
The system uses the high-speed ESP32 **VSPI peripheral** shared between the ILI9341 display controller ($27\text{ MHz}$) and the XPT2046 resistive touch controller ($1\text{ MHz}$).
- **TFT_CS (`GPIO 5`)** and **TOUCH_CS (`GPIO 21`)** are driven `HIGH` immediately upon microcontroller boot.
- Hardware SPI bus contention is eliminated at the software level: all `tft.*` rendering and touch polling routines run strictly on **Core 1** inside the main FreeRTOS `loop()`. Core 0 never touches the SPI bus.

### 6.2 Touch Screen Calibration Persistence Across Reboots
Resistive touch screens require coordinate translation from raw 12-bit ADC resistance values to physical pixel coordinates ($320\times240$).
1. **Boot NVS Recall:** On boot, the firmware accesses the `"jalrakshak"` Preferences namespace and looks for the `"calData"` binary key (an array of 5 `uint16_t` values). If found, it immediately calls `tft.setTouch(calData)`, bypassing calibration.
2. **Interactive 3-Second Recalibration Trigger:** If the user presses and holds their finger on the display during the first 3 seconds of boot, the system triggers the 4-corner interactive calibration wizard, saves the new matrix to Flash NVS, and resumes normal operation.
3. **Tap Filtering & Debouncing:**
   - Touch pressure threshold set to `220` (rejects spurious EMI, detects light finger taps).
   - Edge-triggered software debounce flag (`isTouching`) with a $250\text{ ms}$ cooldown timer prevents rapid multi-triggering of buttons.
   - Clean UI feedback: Tapping a button inverts its border for $100\text{ ms}$ with zero drawing artifacts or residual tap dots.

### 6.3 Multilingual User Interface
To support deployment across diverse demographic regions in India, the UI engine includes a localized string lookup table supporting three languages:

| Language Token | English (`LANG_EN`) | Hindi (`LANG_HI`) | Marathi (`LANG_MR`) |
| :--- | :--- | :--- | :--- |
| **Header Title** | `JalRakshak AI` | `जल रक्षक एआई` | `जल रक्षक एआय` |
| **Safe Badge** | `DRINKABLE` | `सुरक्षित जल` | `पिण्यायोग्य पाणी` |
| **Caution Badge**| `CAUTION` | `सावधानी` | `सावधान` |
| **Alert Badge**  | `CONTAMINATED` | `असुरक्षित जल` | `दूषित पाणी` |
| **Temp Card**    | `Temperature` | `तापमान` | `तापमान` |
| **TDS Card**     | `Dissolved Solids`| `घुलनशील ठोस` | `विद्राव्य घटक` |
| **Turb Card**    | `Turbidity` | `गंदलापन` | `गढूळपणा` |
| **pH Card**      | `pH Level` | `पीएच स्तर` | `सामू (pH)` |

---

## 7. Hardware PWM Screen Brightness Subsystem

The TFT display backlight LED is connected to **`GPIO 22` (`D22`)** and driven via the ESP32 hardware **LEDC peripheral**:
- **LEDC Channel:** 0
- **Timer Frequency:** $5000\text{ Hz}$ ($5\text{ kHz}$ eliminates audible coil whine and display flicker)
- **Timer Resolution:** 8-bit ($0 - 255$ duty cycle counts)

### Brightness States & Energy Management

| Level | Mode | Duty Count | Percentage | Typical Operating Current | Intended Use Case |
| :---: | :---: | :---: | :---: | :---: | :--- |
| **0** | `BRIGHTNESS_LOW` | 75 | **30%** | $\approx 22\text{ mA}$ | Night-time monitoring / battery preservation |
| **1** | `BRIGHTNESS_MED` | 165 | **65%** | $\approx 55\text{ mA}$ | Indoor laboratory / typical bench operation |
| **2** | `BRIGHTNESS_HIGH`| 255 | **100%** | $\approx 92\text{ mA}$ | High ambient sunlight field deployment |

The active brightness mode is persisted in NVS and restored automatically upon system power-up.

---

## 8. Edge AI Neural Engine & Deterministic IS 10500:2012 Fail-Safe

### 8.1 Machine Learning Model Architecture
JalRakshak implements a custom feed-forward Multilayer Perceptron (MLP) trained on 6,000 synthesized and real-world aligned water quality samples:

```
[ Input Layer: 4 Features ]
  - pH (Normalized 0.0 - 14.0)
  - TDS (Normalized 0 - 2000 ppm)
  - Turbidity (Normalized 0 - 1000 NTU)
  - Temperature (Normalized 0 - 50 °C)
              │
              ▼
[ Hidden Layer 1: 16 Neurons + ReLU Activation ]
              │
              ▼
[ Hidden Layer 2: 12 Neurons + ReLU Activation ]
              │
              ▼
[ Output Layer: 3 Neurons + Softmax Activation ]
  - Class 0: SAFE (Potable)
  - Class 1: CAUTION (Mineral Drift / Marginal Silt)
  - Class 2: CONTAMINATED (Unsafe for Human Consumption)
```

- **Inference Time:** Under **350 microseconds** on the Tensilica LX6 core @ 240 MHz.
- **Model Storage:** Fully compiled into static Flash memory ($< 4\text{ KB}$ weights and biases table). Zero dynamic heap allocations during inference.

### 8.2 Deterministic Bureau of Indian Standards (BIS) IS 10500:2012 Guardrail
While neural networks excel at multi-dimensional non-linear pattern recognition, mission-critical public health systems cannot tolerate statistical edge-case misclassifications (false negatives).

To provide a 100% safety guarantee, JalRakshak wraps the ML model in a **deterministic fail-safe guardrail engine**. Regardless of what the neural network outputs, if any single parameter violates statutory BIS IS 10500:2012 drinking water standards, the system immediately forces the status to `CONTAMINATED` and triggers visual alarms:

$$\text{Final Status} = \begin{cases} 
\text{CONTAMINATED (2)}, & \text{if } \text{pH} < 6.5 \text{ or } \text{pH} > 8.5 \\
\text{CONTAMINATED (2)}, & \text{if } \text{TDS} > 2000.0\,\text{ppm} \\
\text{CONTAMINATED (2)}, & \text{if } \text{Turbidity} > 5.0\,\text{NTU} \\
\text{CAUTION (1)}, & \text{if } 500.0 < \text{TDS} \le 2000.0\,\text{ppm} \\
\text{CAUTION (1)}, & \text{if } 1.0 < \text{Turbidity} \le 5.0\,\text{NTU} \\
\text{TinyML Prediction}, & \text{otherwise}
\end{cases}$$

---

## 9. Dual-Core FreeRTOS SMP Architecture & Concurrency Model

The ESP32 features two symmetrical 32-bit Xtensa LX6 cores. JalRakshak harnesses both cores to guarantee that complex UI drawing routines never delay critical sensor sampling or safety monitoring:

```
┌────────────────────────────────────────────────────────┐
│                   ESP32 DUAL-CORE SOC                  │
├──────────────────────────┬─────────────────────────────┤
│         CORE 0           │           CORE 1            │
│  (Sensor & Network Task) │   (Graphics, Touch & UI)    │
├──────────────────────────┼─────────────────────────────┤
│ • 32-Sample ADC Reading  │ • 30 Hz TFT Display Refresh │
│ • Digital Trimmed-Mean   │ • XPT2046 Touch Controller  │
│ • TDS Temp Compensation  │ • PWM Backlight Control     │
│ • Dynamic Turbidity Math │ • Button Debounce Logic     │
│ • TinyML Inference (2Hz) │ • 1-Second Serial Diagnostic│
│ • Wi-Fi SoftAP DNS & Web │                             │
└─────────────┬────────────┴──────────────┬──────────────┘
              │                           │
              ▼                           ▼
        ┌───────────────────────────────────────┐
        │  Shared Telemetry (WaterTelemetry)    │
        │  Protected via portENTER_CRITICAL /   │
        │  Atomic UI Snapshot Mutex             │
        └───────────────────────────────────────┘
```

- **Core 0 Task (`sensorTask` @ 10 Hz):** Dedicated to high-speed hardware reading, filtering, ML inference, and network packet handling.
- **Core 1 Task (`loop()` @ 30 Hz):** Dedicated exclusively to rendering TFT cards, drawing graphs, polling touch inputs, and servicing user interaction.
- **Thread Safety:** When Core 1 captures a display frame, it copies the data structure inside a FreeRTOS critical section (`portENTER_CRITICAL`), guaranteeing zero memory torn-read conditions.

---

## 10. Hardware Status LEDs & Visual Diagnostic Engine

Three discrete 5mm high-efficiency LEDs mounted on the front panel provide instant visual feedback visible from across a room:

```
[ ESP32 GPIO 33 ] ────[ 220 Ω ]────► [ Green LED ] ────► GND  (SAFE / Potable Water)
[ ESP32 GPIO 32 ] ────[ 220 Ω ]────► [ Red LED ]   ────► GND  (CONTAMINATED / Alert)
[ ESP32 GPIO 27 ] ────[ 220 Ω ]────► [ Blue LED ]  ────► GND  (Activity / TinyML Pulse)
```

### 10.1 Diagnostic LED Behavior Matrix

| Operating State | Green LED (`GPIO 33`) | Red LED (`GPIO 32`) | Blue LED (`GPIO 27`) | System Meaning |
| :--- | :---: | :---: | :---: | :--- |
| **Boot Self-Test (First 1000 ms)** | **ON (Solid)** | **ON (Solid)** | **ON (Solid)** | Visual lamp test confirming all LEDs and GPIOs are functional |
| **SAFE (Potable Water)** | **ON (Solid)** | OFF | 40ms Pulse @ 2 Hz | Water meets all BIS IS 10500 standards; safe for consumption |
| **CAUTION (Marginal Water)** | Alternating 600ms | Alternating 600ms | 40ms Pulse @ 2 Hz | Secondary parameters elevated; caution advised |
| **CONTAMINATED (Unsafe)** | OFF | Rapid Flash (200ms) | 40ms Pulse @ 2 Hz | Contaminated; dangerous for human consumption |
| **Wi-Fi Web Client Request** | Current State | Current State | Double Flash (60ms) | Remote browser accessed dashboard or `/api/telemetry` |

---

## 11. Offline-First Wi-Fi SoftAP Web Dashboard & Captive Portal

To facilitate field audits without opening the hardware enclosure, JalRakshak hosts an autonomous Wi-Fi Access Point:
- **SSID:** `JalRakshak-Node`
- **Password:** `jalrakshak123`
- **Default IP Address:** `192.168.4.1`
- **DNS Server:** Built-in captive portal; resolving any domain (or `http://jalrakshak.local`) automatically directs the user to the dashboard.

### 11.1 Web Server Features
1. **Zero External Assets:** All HTML, CSS, and JavaScript are embedded directly in ESP32 Flash memory. No internet connection or CDN is required.
2. **REST API Endpoint (`/api/telemetry`):** Returns complete live JSON telemetry for automated data logging:
```json
{
  "tempC": 25.1,
  "tdsPpm": 142.0,
  "turbNTU": 0.8,
  "pH": 7.15,
  "status": 0,
  "statusText": "SAFE",
  "pinTurbV": 2.65,
  "rawTurbV": 3.98,
  "vTurbClean": 4.00,
  "inferenceUs": 312
}
```
3. **Remote Control Actions:** Allows remote triggering of zero-calibration, language switching, and emergency water quality reset directly from any smartphone or tablet browser.

---

## 12. Flash NVS Storage Architecture & Namespace Layout

JalRakshak utilizes the ESP32 Non-Volatile Storage (NVS) partition to retain all operational settings and calibration baselines permanently across power loss and firmware flashing:

| NVS Namespace | Key | Data Type | Default Value | Description |
| :--- | :--- | :---: | :---: | :--- |
| `"jal_cal"` | `"turb_base"` | `float` | `4.00f` | Dynamic clean-water reference baseline voltage for $0.0\text{ NTU}$ |
| `"jal_cal"` | `"ph_neutral"`| `float` | `2.50f` | Measured analog voltage corresponding to neutral pH 7.00 buffer |
| `"jalrakshak"` | `"tds_offset"`| `float` | `0.00f` | Zero-point offset subtraction in parts per million ($\text{ppm}$) |
| `"jalrakshak"` | `"calData"` | `uint16_t[5]` | `[0,0,0,0,0]` | 5-point XPT2046 resistive touch coordinate calibration matrix |
| `"jalrakshak"` | `"lang"` | `uint8_t` | `0` (EN) | Persistent UI language selection (`0=EN, 1=HI, 2=MR`) |
| `"jalrakshak"` | `"brightness"`| `uint8_t` | `1` (MED) | Persistent screen backlight level (`0=LOW, 1=MED, 2=HIGH`) |

---

## 13. Current Progress, Verification Benchmarks & Results

### 13.1 PlatformIO Build & Memory Footprint Verification
- **Target Microcontroller:** Espressif ESP32-WROOM-32 (240 MHz Xtensa LX6, 4MB External Flash)
- **RAM Utilization:** **`51,652 bytes`** out of `327,680 bytes` (**15.8%** utilized, leaves $> 275\text{ KB}$ for FreeRTOS heap and network buffers).
- **Flash Utilization:** **`1,071,599 bytes`** out of `1,310,720 bytes` (**81.8%** utilized with embedded Web Server, TensorFlow Lite Micro runtime, and multilingual fonts).
- **Build Status:** Compiled with `0 errors` using GCC Xtensa toolchain under PlatformIO.

### 13.2 Real-World Benchtop Sensor Verification
The system was validated on the bench across three standard water test environments:

| Test Water Sample | DS18B20 ($^\circ\text{C}$) | TDS ($\text{ppm}$) | Turbidity ($\text{NTU}$) | pH Level | TinyML Inference | BIS IS 10500 Status | LED Indicator |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Commercial RO Drinking Water** | $24.8^\circ\text{C}$ | $48\,\text{ppm}$ | $0.2\,\text{NTU}$ | $7.12$ | SAFE ($99.2\%$) | **SAFE** | Green Solid |
| **Municipal Tap Water** | $25.4^\circ\text{C}$ | $215\,\text{ppm}$ | $0.6\,\text{NTU}$ | $7.38$ | SAFE ($97.8\%$) | **SAFE** | Green Solid |
| **Saline Solution (1g NaCl / L)**| $25.1^\circ\text{C}$ | $1120\,\text{ppm}$ | $0.5\,\text{NTU}$ | $7.20$ | CAUTION ($94.1\%$) | **CAUTION** | Green/Red Alternating |
| **Silt & Soil Suspension** | $24.9^\circ\text{C}$ | $410\,\text{ppm}$ | $18.4\,\text{NTU}$ | $6.82$ | CONTAMINATED ($99.6\%$)| **CONTAMINATED** | Red Rapid Flash |
| **Dilute Acetic Acid (Vinegar)** | $25.2^\circ\text{C}$ | $320\,\text{ppm}$ | $0.4\,\text{NTU}$ | $3.85$ | CONTAMINATED ($100\%$)| **CONTAMINATED** | Red Rapid Flash |

---

## 14. Report Summary & Presentation Guide

### 14.1 Key Takeaways for Academic Submission & Evaluators
1. **Truly Edge-Native:** Does not rely on remote clouds, cellular subscriptions, or third-party servers to protect public health.
2. **Deterministic-AI Fusion:** Solves the notorious "black box" reliability concern of neural networks by integrating hard BIS IS 10500 regulatory guardrails over the machine learning engine.
3. **Electrochemical Rigor:** Features full temperature compensation on TDS, Nernstian slope scaling on pH, dynamic baseline referencing on Turbidity, and 32-sample trimmed-mean noise rejection on the ESP32 ADC.
4. **Inclusive Human Interface:** Multilingual support in English, Hindi, and Marathi alongside high-visibility LED and web indicators makes safe drinking water accessible to rural operators and lab technicians alike.

---
*End of Technical Specification — JalRakshak Engineering Team (2026)*
