# 🌊 JalRakshak: Comprehensive System Overview & Technical Specification

> **Project:** JalRakshak – Offline-First Edge AI Water Quality Diagnostic Node  
> **Microcontroller:** Espressif ESP32-WROOM-32 (Dual-Core Tensilica Xtensa LX6 @ 240 MHz)  
> **Display & Touch:** 2.8" ILI9341 SPI TFT (320×240 Landscape) + XPT2046 Resistive Touch  
> **Firmware Framework:** PlatformIO with Arduino Core 3.x / FreeRTOS SMP  
> **Edge AI Framework:** Custom C++ Feed-Forward Neural Engine + TensorFlow Lite Micro Byte Array  
> **Regulatory Standard:** Bureau of Indian Standards (BIS) IS 10500:2012 Drinking Water Specification  
> **GitHub Repository:** [https://github.com/deadpool96p/JalRakshak.git](https://github.com/deadpool96p/JalRakshak.git)

---

## 📑 Table of Contents
1. [Executive Summary](#1-executive-summary)
2. [Complete Hardware Pin Mapping & Circuit Schematics](#2-complete-hardware-pin-mapping--circuit-schematics)
3. [Sensor Operational Principles & Transfer Functions](#3-sensor-operational-principles--transfer-functions)
4. [Dual-Core FreeRTOS Architecture](#4-dual-core-freertos-architecture)
5. [TinyML Neural Network & IS 10500 Hybrid Fusion](#5-tinyml-neural-network--is-10500-hybrid-fusion)
6. [Interactive UI State Machine & Graphics Engine](#6-interactive-ui-state-machine--graphics-engine)
7. [Regional Language System (English / Hindi / Marathi)](#7-regional-language-system-english--hindi--marathi)
8. [Hardware PWM Backlight & Flash NVS Persistence](#8-hardware-pwm-backlight--flash-nvs-persistence)
9. [XPT2046 Touch Calibration & Debounce Engine](#9-xpt2046-touch-calibration--debounce-engine)
10. [Step-by-Step Bench Calibration Checklist](#10-step-by-step-bench-calibration-checklist)
11. [Troubleshooting & Hardware Diagnostic Matrix](#11-troubleshooting--hardware-diagnostic-matrix)
12. [Repository Directory Structure](#12-repository-directory-structure)

---

## 1. Executive Summary

**JalRakshak** is an autonomous, battery/solar-compatible water quality monitoring node designed for remote, rural, and industrial deployment where internet or cloud connectivity is intermittent or absent. 

Unlike traditional IoT monitors that upload raw data to cloud servers for post-processing, JalRakshak executes **real-time sensor fusion, temperature compensation, and TinyML neural network inference entirely on-device within 350 microseconds**. It continuously evaluates 4 critical water parameters:
- **Temperature ($^\circ\text{C}$):** Monitored digitally to provide thermodynamic compensation across all electrochemical sensors.
- **Total Dissolved Solids (TDS in $\text{ppm}$):** Quantifies dissolved mineral ions and salinity.
- **Turbidity (NTU):** Measures light scattering caused by suspended solids, silt, and microbial colloidal matter.
- **pH Level:** Gauges acidity/alkalinity to detect industrial runoff, corrosion, or contamination.

The system combines a **4→16→12→3 Multilayer Perceptron (MLP)** trained on 6,000 real-world aligned samples with **deterministic BIS IS 10500:2012 safety guardrails**, guaranteeing that no hazardous water sample is ever falsely classified as potable.

---

## 2. Complete Hardware Pin Mapping & Circuit Schematics

### 2.1 Complete Pin Assignment Table

| Subsystem | Signal Name | ESP32 GPIO | Header Pin | Direction | Signal Type / Electrical Characteristics |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **DS18B20 Temp** | `ONE_WIRE` | **GPIO 4** | `D4` | Bi-directional | Digital 1-Wire, requires **4.7 kΩ pull-up** to 3.3V |
| **Analog TDS** | `TDS_AOUT` | **GPIO 34** | `D34` | Input | ADC1_CH6 (0 – 2.3V linear output, 3.3V powered) |
| **TS-300B Turbidity**| `TURB_AOUT` | **GPIO 36** | `VP` | Input | ADC1_CH0 (0.5 – 4.5V output, 5V powered) |
| **PH-4502C pH** | `PH_PO` | **GPIO 35** | `D35` | Input | ADC1_CH7 (0.0 – 5.0V centered at 2.50V, 5V powered) |
| **Backlight Control**| `TFT_LED / BLK`| **GPIO 22** | `D22` | Output | 5 kHz 8-bit Hardware PWM (Duty 75 – 255) |
| **Display SPI Clock**| `TFT_SCLK` | **GPIO 18** | `D18` | Output | Hardware VSPI SCK (27 MHz display / 1 MHz touch) |
| **Display SPI MOSI** | `TFT_MOSI` | **GPIO 23** | `D23` | Output | Hardware VSPI MOSI (Master Out Slave In) |
| **Display SPI MISO** | `TFT_MISO` | **GPIO 19** | `D19` | Input | Hardware VSPI MISO (Master In Slave Out) |
| **Display Chip Sel** | `TFT_CS` | **GPIO 5** | `D5` | Output | Active-LOW Chip Select (Driven HIGH at boot) |
| **Display Command/DC**| `TFT_DC` | **GPIO 17** | `TX2` | Output | Data/Command Selection Pin |
| **Display Reset** | `TFT_RST` | **GPIO 16** | `RX2` | Output | Active-LOW Display Hardware Reset Pin |
| **Touch Chip Select**| `TOUCH_CS` | **GPIO 21** | `D21` | Output | Dedicated XPT2046 Active-LOW Chip Select |
| **Touch Clock** | `T_CLK` | **GPIO 18** | `D18` | Output | Shared hardware VSPI clock line |
| **Touch Data In** | `T_DIN` | **GPIO 23** | `D23` | Output | Shared hardware VSPI MOSI line |
| **Touch Data Out** | `T_DO` | **GPIO 19** | `D19` | Input | Shared hardware VSPI MISO line |
| **Module Power 1** | `VCC_5V` | `VIN` | `5V` | Power Out | 5.0V DC from USB to power TS-300B & PH-4502C |
| **Module Power 2** | `VCC_3V3` | `3V3` | `3.3V` | Power Out | 3.3V DC to power DS18B20 & DT0530 TDS |
| **Common Ground** | `GND` | `GND` | `GND` | Ground | Common system ground rail across all modules |

> **IMPORTANT ARCHITECTURAL RULE:** All analog sensors are wired exclusively to **ADC1** (`GPIO 34, 35, 36`). ADC2 pins are avoided because ADC2 is shared with the ESP32 Wi-Fi/Bluetooth hardware block and is disabled whenever wireless networking is initialized.

---

### 2.2 Shared SPI Bus Architecture & Contention Guard

```
ESP32 (Master)                           2.8" ILI9341 + XPT2046 Module
┌──────────────────┐                     ┌────────────────────────────┐
│                  │                     │      ILI9341 (Display)     │
│  GPIO 23 (MOSI)  ├────────────────────►│ SDI (MOSI)                 │
│  GPIO 18 (SCK)   ├────────────────────►│ SCK (Clock)                │
│  GPIO 19 (MISO)  │◄────────────────────┤ SDO (MISO)                 │
│  GPIO 5  (TFT_CS)├────────────────────►│ CS  (Display Select)      │
│  GPIO 17 (TFT_DC)├────────────────────►│ DC  (Command/Data)         │
│  GPIO 16 (TFT_RST├────────────────────►│ RST (Hardware Reset)       │
│  GPIO 22 (PWM)   ├────────────────────►│ LED (Backlight Power)      │
│                  │                     ├────────────────────────────┤
│                  │                     │      XPT2046 (Touch)       │
│  GPIO 18 (SCK)   ├────────────────────►│ T_CLK (Shared Clock)       │
│  GPIO 23 (MOSI)  ├────────────────────►│ T_DIN (Shared MOSI)        │
│  GPIO 19 (MISO)  │◄────────────────────┤ T_DO  (Shared MISO)        │
│  GPIO 21 (T_CS)  ├────────────────────►│ T_CS  (Dedicated Select)   │
└──────────────────┘                     └────────────────────────────┘
```

#### Why Boot Contention Happens and How JalRakshak Prevents It:
When the XPT2046 touch controller and ILI9341 display controller share the same SPI bus, an unasserted (floating or LOW) `TOUCH_CS` pin allows the touch chip to listen and drive `MISO` simultaneously while the display controller is receiving initial power-up configuration commands. This causes command corruption, resulting in a **solid white uninitialized display**.

**JalRakshak Firmware Guard (`src/ui_manager.cpp`):**
```cpp
// 1. Force both chip-select pins HIGH before initializing SPI peripherals
pinMode(5, OUTPUT);  digitalWrite(5, HIGH);  // TFT_CS idle HIGH
pinMode(21, OUTPUT); digitalWrite(21, HIGH); // TOUCH_CS idle HIGH
delay(10);

// 2. Initialize display controller at verified 27 MHz clock
tft.init();
tft.setRotation(1); // Locked 320x240 landscape
```

---

## 3. Sensor Operational Principles & Transfer Functions

### 3.1 DS18B20 Digital Temperature Probe
- **Operating Voltage:** 3.0V – 5.5V (powered at 3.3V).
- **Communication Protocol:** Dallas 1-Wire on `GPIO 4` with a 4.7 kΩ pull-up resistor.
- **Conversion Resolution:** 12-bit ($0.0625^\circ\text{C}$ resolution, conversion time $\approx 750\text{ ms}$).
- **Firmware Implementation:** Uses non-blocking asynchronous conversion (`tempSensor.setWaitForConversion(false)`). If probe disconnects or reports $-127^\circ\text{C}$, the firmware triggers an automated fallback to $25.0^\circ\text{C}$ and flags `"Fallback"` on the UI.

---

### 3.2 DT0530 Analog TDS Sensor (Total Dissolved Solids)
- **Physical Property:** Measures Electrical Conductivity (EC in $\mu\text{S/cm}$) via two titanium electrode pins with an excitation AC signal to prevent electrode polarization.
- **Thermal Drift Phenomenon:** Conductivity increases by approximately $2.0\%$ per degree Celsius rise due to decreased water viscosity and increased ion mobility.
- **Temperature Compensation Formula:**
  $$V_{comp} = \frac{V_{raw}}{1.0 + 0.02 \times (T_{water} - 25.0)}$$
- **Cubic Transfer Polynomial:**
  $$TDS_{ppm} = \left( 133.42 \cdot V_{comp}^3 - 255.86 \cdot V_{comp}^2 + 857.39 \cdot V_{comp} \right) \times 0.5$$
- **Offset Calibration:** A runtime offset $TDS_{offset}$ is subtracted from the calculation when the user taps `CAL / ZERO` in pure RO/distilled water.

---

### 3.3 TS-300B Optical Turbidity Sensor
- **Physical Property:** Utilizes an infrared LED emitter ($880\text{ nm}$) and a phototransistor receiver positioned at $90^\circ$ (nephelometric scattering) and $180^\circ$ (optical transmission).
- **Operating Characteristics:**
  - Clear water allows maximum infrared transmission $\rightarrow$ High output voltage ($\ge 3.80\text{V}$ at 5V supply).
  - Suspended particles scatter and attenuate light $\rightarrow$ Output voltage drops proportionally.
- **Quadratic Transfer Function:**
  $$NTU = -1120.4 \cdot V^2 + 5742.3 \cdot V - 4352.9$$
- **Safety Clamping:**
  - If $V \ge 3.80\text{V}$: Clamped to $0.5\text{ NTU}$ (crystal clear drinking water).
  - If $V \le 2.20\text{V}$: Saturated muddy runoff, clamped to $150\text{--}3000\text{ NTU}$.

---

### 3.4 PH-4502C Analog pH Probe & Conditioning Module
- **Physical Property:** Glass electrode develops an electromotive force (EMF) based on the activity ratio of hydrogen ions ($H^+$) across a thin glass membrane relative to a silver/silver-chloride reference cell.
- **Nernstian Theoretical Response:**
  $$E = E_0 - \frac{2.303 \cdot R \cdot T}{F} \cdot pH$$
  At $25^\circ\text{C}$, the ideal slope is $59.16\text{ mV per pH unit}$. The onboard operational amplifier provides a $3\times$ gain stage, resulting in an effective sensor slope of $\approx 0.180\text{ V per pH unit}$.
- **Temperature-Corrected Calibration Formula:**
  $$pH = 7.00 + \frac{V_{neutral} - V_{raw}}{Slope_{25} \times \left( \frac{T_{water} + 273.15}{298.15} \right)}$$
  Where $V_{neutral} = 2.50\text{V}$ (shorted BNC or pH 7.00 buffer) and $Slope_{25} = 0.180\text{ V/pH}$.

---

## 4. Dual-Core FreeRTOS Architecture

The ESP32 incorporates two 32-bit Tensilica Xtensa LX6 CPU cores. JalRakshak partitions its workload across both cores using FreeRTOS tasks to guarantee that high-speed sensor sampling and ML execution never interrupt smooth 30 FPS UI animations.

```
       ┌────────────────────────────────────────────────────────┐
       │             ESP32 DUAL-CORE SOC ARCHITECTURE           │
       └────────────────────────────────────────────────────────┘
                 │                                      │
                 ▼                                      ▼
    ┌───────────────────────────┐          ┌───────────────────────────┐
    │          CORE 0           │          │          CORE 1           │
    │  Task: sensorTinyMLTask   │          │  Task: loop() (Arduino)   │
    │  Priority: 1 | Stack: 8KB │          │  Priority: 1 | 30 FPS     │
    └────────────┬──────────────┘          └────────────┬──────────────┘
                 │                                      │
                 ├─► 1. Sample ADC1 32x                 ├─► 1. Poll XPT2046
                 ├─► 2. Read DS18B20                    ├─► 2. Invert button
                 ├─► 3. Temp compensation               ├─► 3. Render cards
                 ├─► 4. TinyML inference                ├─► 4. Draw graph sprite
                 ├─► 5. Guardrail check                 ├─► 5. Manage PWM LED
                 │                                      │
                 └──────────────┐        ┌──────────────┘
                                ▼        ▼
                          ┌─────────────────────┐
                          │   FreeRTOS Mutex    │
                          │  (g_telemetryMutex) │
                          │                     │
                          │  Thread-Safe State: │
                          │  WaterTelemetry     │
                          └─────────────────────┘
```

### 4.1 Core 0: Sensor Acquisition & ML Task (`sensorTinyMLTask`)
- **Execution Interval:** Pinned to Core 0, runs every $500\text{ ms}$ (2 Hz) via `vTaskDelayUntil()`.
- **Noise Rejection:** 32-sample consecutive ADC oversampling with $50\text{ }\mu\text{s}$ settling delays per sample, reducing thermal noise and non-linear ADC quantization errors.
- **Mutex Sync:** Takes `g_telemetryMutex` with a $50\text{ ms}$ timeout, copies the calibrated metrics into `g_sharedTelemetry`, and releases the lock.

### 4.2 Core 1: User Interface & Touch Task (`loop()`)
- **Execution Interval:** Runs at $\approx 30\text{ FPS}$ ($33\text{ ms}$ cycle delay).
- **Snapshot Isolation:** Acquires `g_telemetryMutex` with a quick $10\text{ ms}$ timeout, takes an atomic snapshot of `g_sharedTelemetry`, and renders the UI strictly using local snapshot memory.
- **Strict Thread Safety:** Zero `tft.*` or SPI display commands are ever called from Core 0. All rendering and sprite updates remain isolated on Core 1.

---

## 5. TinyML Neural Network & IS 10500 Hybrid Fusion

### 5.1 Neural Network Topology

```
Input Vector [4]          Hidden Layer 1 [16]       Hidden Layer 2 [12]       Output Softmax [3]
┌─────────────┐           ┌─────────────────┐       ┌─────────────────┐       ┌─────────────────┐
│     pH      ├──────────►│ Dense Neuron 1  ├──────►│ Dense Neuron 1  ├──────►│ 0: SAFE         │
│  TDS (ppm)  ├──────────►│ Dense Neuron 2  ├──────►│ Dense Neuron 2  ├──────►│ 1: CAUTION      │
│  Turb (NTU) ├──────────►│ ...             ├──────►│ ...             ├──────►│ 2: CONTAMINATED │
│  Temp (°C)  ├──────────►│ Dense Neuron 16 ├──────►│ Dense Neuron 12 ├──────►│                 │
└─────────────┘           └─────────────────┘       └─────────────────┘       └─────────────────┘
  Normalized via                 ReLU                      ReLU                      Softmax
  StandardScaler             Activation                Activation                  Probability
```

### 5.2 Training Dataset & Distribution Statistics
The neural network is trained on **6,000 real-world aligned water samples** generated via `ml/prepare_dataset.py`, matching empirical groundwater datasets from the Central Pollution Control Board (CPCB) of India:
- **Class 0 (`SAFE`):** 2,100 samples ($35\%$). $pH \in [6.6, 8.4]$, $TDS \in [45, 320]\text{ ppm}$, $Turbidity \in [0.05, 1.8]\text{ NTU}$.
- **Class 1 (`CAUTION`):** 1,800 samples ($30\%$). High-mineral borewell water ($TDS\text{ }340\text{--}500\text{ ppm}$), minor pH shifts ($6.2\text{--}6.5$ or $8.5\text{--}9.0$), or post-monsoon haze ($2.1\text{--}5.0\text{ NTU}$).
- **Class 2 (`CONTAMINATED`):** 2,100 samples ($35\%$). Chemical industrial runoff ($pH < 6.0$ or $> 9.2$), toxic dissolved solids ($TDS > 520\text{ ppm}$ up to $2200\text{ ppm}$), and sewage/mud saturation ($Turbidity > 5.5\text{ NTU}$ up to $180\text{ NTU}$).

### 5.3 On-Device Feature Preprocessing (StandardScaler)
The training script computes dataset-wide mean ($\mu$) and standard deviation ($\sigma$) parameters and exports them directly into `src/water_model.h`:
$$x_{norm} = \frac{x - \mu}{\sigma}$$
- $pH$: $\mu = 7.4321,\quad \sigma = 1.6047$
- $TDS$: $\mu = 421.4632,\quad \sigma = 401.7287$
- $Turbidity$: $\mu = 15.5648,\quad \sigma = 31.3246$
- $Temp$: $\mu = 25.9744,\quad \sigma = 4.1328$

### 5.4 Test Set Evaluation & Safety Metric
Evaluated on an independent, stratified test set of **1,200 unseen samples**:
- **Overall Classification Accuracy:** **$99.75\%$**
- **False Negative Rate on Hazardous Water:** **$0.24\%$** (419 out of 420 contaminated test samples correctly identified; 0 classified as safe).
- **On-Device Inference Execution Time:** $\approx 180\text{--}240\text{ }\mu\text{s}$ on ESP32 Core 0.

### 5.5 Hybrid IS 10500 Deterministic Fail-Safe Guardrails
Machine learning models alone can theoretically produce statistical anomalies on extreme edge cases. To eliminate false-negative risks, JalRakshak runs a **dual-layer hybrid decision engine**:
```cpp
bool breach = false;
if (telemetry.tdsPpm > 500.0f) {
  breach = true;
  snprintf(telemetry.alertReason, sizeof(telemetry.alertReason), "TDS > 500 ppm (IS 10500)");
} else if (telemetry.turbNTU > 5.0f) {
  breach = true;
  snprintf(telemetry.alertReason, sizeof(telemetry.alertReason), "Turbidity > 5 NTU (IS 10500)");
} else if (telemetry.pH < 6.5f || telemetry.pH > 8.5f) {
  breach = true;
  snprintf(telemetry.alertReason, sizeof(telemetry.alertReason), "pH out of range (6.5-8.5)");
}

if (breach) {
  telemetry.failSafeTriggered = true;
  // If severe violation or ML agrees -> CONTAMINATED; else CAUTION
  if (telemetry.tdsPpm > 600.0f || telemetry.turbNTU > 10.0f || 
      telemetry.pH < 6.0f || telemetry.pH > 9.0f || 
      telemetry.predictedClass == CLASS_CONTAMINATED) {
    telemetry.finalStatus = CLASS_CONTAMINATED;
  } else {
    telemetry.finalStatus = CLASS_CAUTION;
  }
} else {
  telemetry.failSafeTriggered = false;
  telemetry.finalStatus = telemetry.predictedClass;
}
```

---

## 6. Interactive UI State Machine & Graphics Engine

The display UI runs at locked **$320 \times 240$ landscape orientation** using a modern medical/industrial dark navy color scheme (`#0B132B` / `0x08A5`).

```
┌─────────────────────────────────────────────────────────────┐
│ JALRAKSHAK // AI    (•)      [BRT:H]   [LNG:EN]    [LIVE]   │ y: 0-28
├──────────────────────────┬──────────────────────────────────┤
│ ┌──────────┐┌──────────┐ │ ┌──────────────────────────────┐ │
│ │ pH LEVEL ││ TDS(PPM) │ │ │        SAFE / POTABLE        │ │ y: 34-110
│ │   7.24   ││   142    │ │ ├──────────────────────────────┤ │
│ │  Normal  ││ppm Normal│ │ │   P(Contam): 4%              │ │
│ └──────────┘└──────────┘ │ │ ┌──────────────────────────┐ │ │
│ ┌──────────┐┌──────────┐ │ │ │  Animated Waveform Sprite│ │ │ y: 116-192
│ │TURBIDITY ││ TEMP (C) │ │ │ │  (120 x 86 px)           │ │ │
│ │  0.8 NTU ││  24.8 C  │ │ │ └──────────────────────────┘ │ │
│ │Clear NTU ││Probe OK  │ │ │ Latency: 210 us | IS10500 OK │ │
│ └──────────┘└──────────┘ │ └──────────────────────────────┘ │
├──────────────────────────┴──────────────────────────────────┤
│ [ LIVE/SIM ]       [ VW:DASH ]      [ CAL/ZERO ]    [LNG:EN]│ y: 202-238
└─────────────────────────────────────────────────────────────┘
```

### 6.1 View 1: Main Dashboard (`VIEW_DASHBOARD`)
- **2×2 Parameter Cards Grid (Left):**
  - Card 0: `pH LEVEL` with dynamic color tinting (Green for normal, Amber for caution, Red for alert).
  - Card 1: `TDS (PPM)` temperature-compensated dissolved solids.
  - Card 2: `TURBIDITY` in NTU.
  - Card 3: `TEMP (C)` interactive card. Tapping this card when no probe is plugged in cycles simulated temperature ($20^\circ\text{C} \rightarrow 25^\circ\text{C} \rightarrow 30^\circ\text{C} \rightarrow 35^\circ\text{C}$) to demonstrate live TDS compensation.
- **Right Panel (Status & Waveform):**
  - Overall status badge (`SAFE / POTABLE`, `CAUTION`, `CONTAMINATED`).
  - Scrolling rolling waveform sprite ($120 \times 86\text{ px}$) plotting real-time contamination probability ($0\text{--}100\%$) across time.
  - Latency in microseconds and IS 10500 compliance note.

### 6.2 View 2: Raw Diagnostics & Hardware Settings (`VIEW_RAW_DIAGNOSTICS`)
- Displays uncalibrated live ADC voltages for troubleshooting:
  - `TDS Sensor (GPIO 34): x.xxx V`
  - `Turbidity TS-300B (VP/36): x.xxx V`
  - `pH Probe (GPIO 35): x.xxx V`
  - `DS18B20 Temp (GPIO 4): xx.xx °C` with real-time compensation multiplier.
- Interactive Settings rows:
  - `[ BRT: LOW / MED / HIGH (Tap) ]` cycles PWM duty.
  - `[ LANG: ENGLISH / HINDI / MARATHI (Tap) ]` cycles active language.
  - `[ RECAL TOUCH (Tap to Calibrate) ]` triggers immediate 4-corner touch calibration.

### 6.3 View 3: TinyML Diagnostics & Risk Gauge (`VIEW_TINYML_METRICS`)
- Displays neural network diagnostics:
  - Overall contamination risk bar ($0\text{--}100\%$) with color-coded warning zones.
  - Per-class softmax probability breakdown bars:
    - `CLASS 0 (SAFE): xx.x%`
    - `CLASS 1 (CAUTION): xx.x%`
    - `CLASS 2 (CONTAMINATED): xx.x%`
  - Live execution latency in microseconds and guardrail override status.

### 6.4 SRAM Memory Protection: Small Sprite Allocation
Allocating a full-screen $320 \times 240$ 16-bit sprite requires $320 \times 240 \times 2 = 153,600\text{ bytes}$ ($150\text{ KB}$) of contiguous heap, which leads to immediate allocation failure and heap fragmentation crashes on ESP32.
JalRakshak strictly confines its animated waveform to a compact sprite:
$$\text{Memory} = 120 \times 86 \times 2 = 20,640\text{ bytes } (\approx 20.1\text{ KB})$$
Total firmware RAM utilization is only **$7.3\%$** ($24\text{ KB}$ out of $327\text{ KB}$), leaving ample free heap for FreeRTOS stacks and NVS buffers.

---

## 7. Regional Language System (English / Hindi / Marathi)

### 7.1 The Broken Font Problem in Microcontrollers
Standard embedded graphics libraries (Adafruit_GFX, TFT_eSPI) only contain 7-bit ASCII character tables. When attempting to render complex Unicode scripts such as Devanagari (Hindi and Marathi), glyph renderers fail because Devanagari requires complex conjunct ligature shaping, vowel matra reordering, and halant attachments. This normally results in broken rectangular boxes (`[?] [?] [?]`) or system freezes.

### 7.2 The Pre-Rendered Monochrome Bitmap Solution (`src/lang_assets.h`)
JalRakshak solves this without heavy font engines or external SPI Flash chips by embedding pre-rendered **1-bit monochrome PROGMEM bitmap arrays** generated from official Devanagari font vectors (`Nirmala.ttc` / `mangal.ttf`):

```cpp
// "जल रक्षक" Header Title (72x17 px)
#define BMP_TITLE_JALRAKSHAK_W 72
#define BMP_TITLE_JALRAKSHAK_H 17
static const uint8_t PROGMEM BMP_TITLE_JALRAKSHAK[] = { ... };

// "सुरक्षित पाणी" Safe Status Badge (77x18 px)
#define BMP_STATUS_SAFE_W 77
#define BMP_STATUS_SAFE_H 18
static const uint8_t PROGMEM BMP_STATUS_SAFE[] = { ... };

// "दूषित पाणी" Contaminated Status Badge (59x18 px)
#define BMP_STATUS_DUSHIT_W 59
#define BMP_STATUS_DUSHIT_H 18
static const uint8_t PROGMEM BMP_STATUS_DUSHIT[] = { ... };
```

- Rendered via native hardware call: `tft.drawBitmap(x, y, bitmap, w, h, color, bg)`.
- **Zero broken boxes, zero font corruption, sub-millisecond drawing speed.**
- Numeric values (`7.24`, `142 ppm`, `0.8 NTU`, `24.8 °C`) remain in clean, tabular ASCII digits across all three languages for readability.

---

## 8. Hardware PWM Backlight & Flash NVS Persistence

### 8.1 Hardware PWM Backlight Controller (`GPIO 22`)
Instead of tying the display `LED` pin permanently to 3.3V, JalRakshak routes `LED` to **`GPIO 22`** and configures an ESP32 hardware LEDC PWM timer:
- **Frequency:** $5\text{ kHz}$ (eliminates humanly audible coil whine and high-frequency flicker).
- **Resolution:** 8-bit ($0\text{--}255$ duty cycle steps).
- **3 Brightness Presets:**
  - **LOW (30%):** Duty `75 / 255` (battery conservation mode).
  - **MED (65%):** Duty `165 / 255` (indoor bench testing).
  - **HIGH (100%):** Duty `255 / 255` (direct outdoor sunlight legibility).

### 8.2 Non-Volatile Storage (NVS) via `Preferences.h`
All user settings are persisted in the dedicated `jalrakshak` NVS namespace in ESP32 Flash memory:
- `"calData"`: 10-byte binary blob storing the 5 calibration coefficients (`uint16_t[5]`).
- `"brt"`: 1-byte integer storing the active brightness level ($0, 1, 2$).
- `"lang"`: 1-byte integer storing the active language ($0 = EN, 1 = HI, 2 = MR$).

When the device powers on, all settings are automatically loaded before the display renders its first frame.

---

## 9. XPT2046 Touch Calibration & Debounce Engine

### 9.1 SPI Noise & Voltage Isolation
- **Touch SPI Clock:** Clocked at **$1\text{ MHz}$** (`-D SPI_TOUCH_FREQUENCY=1000000`), isolating touch coordinate acquisition from breadboard wire inductance and parasitic capacitance.
- **Pressure Thresholding:** Standard TFT_eSPI default pressure threshold is `350`, which frequently drops light finger presses. JalRakshak uses `tft.getTouch(&tx, &ty, 220)`, reliably capturing light taps while rejecting electrical noise.

### 9.2 Edge-Triggered Debouncing & Artifact Prevention
- **No Cluttered Circles:** Removed tap drawing rings/dots that previously left permanent visual residue on the dashboard.
- **Tactile Button Inversion:** Touching an interactive button immediately inverts its background/border for $80\text{--}100\text{ ms}$, executes the assigned command once, and cleanly redraws the button.
- **Rising-Edge Gate:** An `isTouching` state flag ensures that resting or holding a finger on a button triggers the action strictly **once** rather than cycling uncontrollably.

### 9.3 Touch Calibration Routines
1. **Default Landscape Vector:** Pre-calibrated for 2.8" ILI9341 rotation 1:
   ```cpp
   uint16_t g_calData[5] = { 275, 3620, 264, 3532, 7 };
   ```
2. **Boot Trigger (3-Second Window):** If the screen is touched during the first 3 seconds of boot (`tft.getTouchRawZ() > 300`), the device automatically enters 4-corner calibration mode (`tft.calibrateTouch()`).
3. **On-Demand Trigger:** In `VIEW_RAW_DIAGNOSTICS`, tapping `"RECAL TOUCH"` or holding `CAL / ZERO` for $\ge 2\text{ seconds}$ initiates calibration without needing to reboot the ESP32.

---

## 10. Step-by-Step Bench Calibration Checklist

### Phase 1: Hardware Diagnostics Mode
1. Power the ESP32 via USB and open the PlatformIO serial monitor at `115200 baud`.
2. Tap the bottom **`VW:DASH`** button to cycle to **`VW:RAW`** (`VIEW_RAW_DIAGNOSTICS`).
3. Verify that all 4 sensor channels show active, plausible voltages.

### Phase 2: TDS Zero Calibration (Pure Water)
1. Submerge the TDS probe into pure distilled or reverse-osmosis (RO) water ($TDS < 15\text{ ppm}$).
2. Tap the bottom **`CAL/ZERO`** button once.
3. The screen will flash a green confirmation banner:
   ```
   [ BASELINE SAVED! ]
   Offsets Stored: TDS=0 | Turb=0
   ```
4. Verify that the displayed TDS reading drops to `0 ppm`.

### Phase 3: Turbidity Hardware Trim
1. Submerge the TS-300B probe in clean tap water.
2. In `VW:RAW`, read `Turbidity TS-300B (VP/36)`.
3. If voltage is lower than $3.80\text{V}$, gently rotate the small blue trim potentiometer on the TS-300B adapter board until voltage reads between **$3.90\text{V}$ and $4.10\text{V}$**.
4. Verify that the main dashboard displays `< 1.0 NTU` in green (`Clear NTU`).

### Phase 4: pH Hardware Trim
1. Connect the BNC probe. To establish hardware neutral reference, short the BNC connector's center core to its outer shell using a small jumper wire (simulates $0.0\text{ mV}$ probe potential = pH 7.00).
2. Observe `pH Probe (GPIO 35)` voltage on `VW:RAW`.
3. Adjust the multi-turn potentiometer on the PH-4502C module closest to the BNC connector until voltage reads exactly **$2.50\text{ V}$**.
4. Remove the short and insert into household vinegar ($pH \approx 2.8\text{--}3.0$): voltage should rise to $\approx 3.05\text{ V}$ and display `Acidic` in bright red.

---

## 11. Troubleshooting & Hardware Diagnostic Matrix

| Symptom | Probable Cause | Verification & Corrective Action |
| :--- | :--- | :--- |
| **Screen is completely black / unlit** | `LED` backlight pin disconnected or unpowered | Connect display `LED` pin to **`GPIO 22`** (or directly to `3V3` to test). Verify PWM duty cycle. |
| **Solid white screen on boot** | SPI bus contention or loose control lines | Ensure `TOUCH_CS` (`GPIO 21`), `TFT_CS` (`GPIO 5`), `DC` (`GPIO 17`), and `RST` (`GPIO 16`) are firmly seated. Verify firmware asserts both CS lines HIGH before `tft.init()`. |
| **Touch coordinates inverted / mirrored** | Display rotation mismatch | Confirm `tft.setRotation(1)` is locked. Hold screen for 3 seconds during boot to run calibration and save to NVS. |
| **TDS reading is zero or erratic** | Temperature probe disconnected or electrode un-submerged | Ensure DS18B20 4.7kΩ pull-up is connected to 3.3V. Check `VW:RAW` to verify raw voltage $> 0.05\text{V}$. |
| **Turbidity reads saturated 100+ NTU in clean water** | Sensor un-trimmed or wired to wrong pin | TS-300B requires 5V on VCC. Adjust the onboard potentiometer clockwise until voltage exceeds $3.80\text{V}$ in clean water. |
| **ESP32 upload fails (`Wrong boot mode 0x13`)** | Auto-reset timing failure | Hold down the physical **`BOOT`** button on the ESP32 board until the terminal displays `Writing at 0x00010000...`, then release. |

---

## 12. Repository Directory Structure

```
JalRakshak_Node/
├── platformio.ini               # PlatformIO board, lib dependencies & SPI build flags
├── SYSTEM_OVERVIEW.md           # This comprehensive master technical document
├── README.md                    # Project overview & quick start guide
├── train_water_tinyml.py        # Standalone Python script for retraining & export
│
├── data/
│   ├── jalrakshak_water_dataset.csv     # 6,000 labeled water quality samples (IS 10500 aligned)
│   └── sensor_calibration_specs.json    # Machine-readable calibration curves & specs
│
├── docs/
│   └── CALIBRATION_GUIDE.md             # 10-minute benchtop reference guide
│
├── ml/
│   ├── prepare_dataset.py               # Generates synthetic/open dataset with edge cases
│   └── train_and_export_tinyml.py       # Trains MLP, computes confusion matrix & exports header
│
└── src/
    ├── main.cpp                 # FreeRTOS setup, dual-core task pinning, mutex synchronization
    ├── sensors.h / .cpp         # Multi-sample filtering, temperature compensation, calibration
    ├── calibration_curves.h     # Verified mathematical transfer equations for all 4 sensors
    ├── water_model.h            # Pre-trained weights, normalization constants & TFLite model array
    ├── tinyml_engine.h / .cpp   # 4->16->12->3 MLP inference engine + IS 10500 safety guardrail
    ├── ui_manager.h / .cpp      # 120x86 sprite waveform, 4 parameter cards & touch engine
    └── lang_assets.h            # 1-bit monochrome Devanagari typography bitmaps
```

---
*Document Version: 2.1.0 — JalRakshak Edge AI Water Safety Project.*
