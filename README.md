# 💧 JalRakshak: Edge AI Offline-First Water Quality Monitoring Node

**JalRakshak** is an offline-first, embedded water quality diagnostic node powered by an **ESP32-WROOM-32** microcontroller, a **2.8" ILI9341 SPI TFT touchscreen** with an **XPT2046** resistive touch controller, and a local **TinyML Neural Network** running real-time anomaly detection backed by deterministic **IS 10500** drinking water safety guardrails.

---

## 🌟 Key Features

- **Comprehensive Technical Documentation:** See the complete architectural guide in [SYSTEM_OVERVIEW.md](file:///c:/Users/soham/OneDrive/Documents/CPP%20sem%205/JalRakshak_Node/SYSTEM_OVERVIEW.md) and bench calibration in [CALIBRATION_GUIDE.md](file:///c:/Users/soham/OneDrive/Documents/CPP%20sem%205/JalRakshak_Node/docs/CALIBRATION_GUIDE.md).
- **FreeRTOS Dual-Core Architecture:**
  - **Core 0:** Multi-sample ADC1 filtering (32-sample averaging), temperature-compensated TDS, TS-300B turbidity polynomial modeling, and 4→16→12→3 TinyML anomaly inference ($< 350\text{ }\mu\text{s}$ latency).
  - **Core 1:** Smooth $\approx 30\text{ FPS}$ flicker-free UI rendering, compact sprite animations, and edge-triggered touch processing.
- **Local TinyML Anomaly Detection:**
  - 4-feature normalized input: $[pH, TDS, Turbidity, Temperature]$.
  - Output softmax classification: `0 = SAFE`, `1 = CAUTION / DRIFT`, `2 = CONTAMINATED`.
  - Fail-safe hybrid fusion with Indian Standard **IS 10500** limits ($pH \in [6.5, 8.5]$, $TDS < 500\text{ ppm}$, $Turbidity < 5\text{ NTU}$).
- **Hardware Status & Activity LEDs:**
  - `GPIO 33` (Green): Solid `HIGH` on Drinkable (`SAFE`); slow alternating flash (`600 ms`) on `CAUTION`.
  - `GPIO 32` (Red): Rapid emergency flash (`200 ms`) on `CONTAMINATED`; slow alternating flash on `CAUTION`.
  - `GPIO 27` (Blue): Crisp `40 ms` pulse on every TinyML cycle, and double-pulse on web client commands.
- **PAM8403 DAC Audio Alert Engine:**
  - Driven by ESP32 Hardware **DAC1 (`GPIO 25`)** feeding PAM8403 amplifier and $2\times 8\Omega$ speakers.
  - Asynchronous FreeRTOS task with zero background hiss (`dacWrite(25, 0)` at idle).
  - Melodic startup chime, confirmation bell on `SAFE`, and urgent warble alarm on `CONTAMINATED` (with 15-second anti-spam cooldown).
  - Audio Mute toggle persisted in Flash NVS.
- **Offline Wi-Fi SoftAP & Web Dashboard:**
  - On-site Access Point: SSID `JalRakshak-Node`, Password `jalrakshak123`, Default IP `192.168.4.1`, mDNS `http://jalrakshak.local`.
  - Low RF power (`11 dBm`) to eliminate ADC1 sensor interference.
  - Captive portal redirection for smartphones and PCs.
  - Real-time responsive medical/industrial dashboard with 60s live HTML5 `<canvas>` telemetry charts and **"Download CSV Log"** button.
  - Bidirectional remote control synchronized with physical TFT touchscreen.
- **Interactive 2.8" ILI9341 Touchscreen UI:**
  - **Multi-Screen State Machine:**
    1. `VIEW: DASH`: 4 parameter cards + live animated contamination risk probability waveform sprite ($120 \times 86\text{ px}$, $20.6\text{ KB SRAM}$).
    2. `VIEW: RAW`: Live raw sensor voltages, temperature compensation factor, active baseline offsets, and hardware settings. Includes on-screen `[ RESET ]` button to clear sensor calibration offsets.
    3. `VIEW: ML`: Large contamination risk gauge bar ($0\text{--}100\%$), per-class softmax distribution bars, and guardrail compliance breakdown.
  - **Multilingual Support:** English, Hindi (हिंदी), and Marathi (मराठी) with pixel-perfect 1-bit monochrome Devanagari typography.
  - **Hardware PWM Backlight:** 3 brightness presets on `GPIO 22` (`LOW 30%`, `MED 65%`, `HIGH 100%`) saved in Flash NVS.
  - **Flash NVS Persistence:** Permanent storage in `<Preferences.h>` for touch calibration (`calData`), backlight brightness, regional language, and sensor zero baselines (`tdsOff` and `turbOff`).
  - **Outlier Rejection & Standby Guard:** Factory eFuse `analogReadMilliVolts` with a 32-sample trimmed-mean filter (discards 4 lowest and 4 highest outliers). Automatic standby guard (`< 0.25V`) prevents false alarms when breadboard wires are unplugged.

---

## 📌 Hardware Pin Mapping

| Subsystem | Signal / Sensor | ESP32 Pin | Details |
| :--- | :--- | :--- | :--- |
| **Temperature** | DS18B20 Probe | `GPIO 4` (`D4`) | 1-Wire Data with 4.7kΩ pull-up ($25.0^\circ\text{C}$ fallback) |
| **TDS Sensor** | Analog TDS Signal | `GPIO 34` (`D34`) | ADC1 Ch6, temperature-compensated (3.3V direct) |
| **Turbidity** | TS-300B Analog Out | `GPIO 36` (`VP`) | ADC1 Ch0 via **10k + 20k divider** (1.50× scale to protect 3.3V ADC) |
| **pH Sensor** | Analog pH `Po` Pin | `GPIO 35` (`D35`) | ADC1 Ch7 via **10k + 20k divider** (1.50× scale; `PH_SENSOR_CONNECTED` toggle) |
| **Status LED Green** | Drinkable / Safe | `GPIO 33` (`D33`) | Solid Green on SAFE; 600ms alternating blink on CAUTION |
| **Status LED Red** | Alert / Contaminated | `GPIO 32` (`D32`) | Rapid 200ms flash on CONTAMINATED |
| **Activity LED Blue**| Inference & Web Pulse | `GPIO 27` (`D27`) | 40ms pulse on ML inference; double-pulse on web requests |
| **PAM8403 DAC Audio**| Amplifier Input | `GPIO 25` (`D25`) | Hardware DAC1 output; zero idle hiss (0V at idle) |
| **Backlight PWM** | Display `LED` / `BLK` | `GPIO 22` (`D22`) | Hardware PWM (5 kHz, 8-bit timer) |
| **Display SPI** | TFT MOSI / SCLK / MISO | `GPIO 23 / 18 / 19` | Hardware VSPI Bus |
| **Display Control** | TFT CS / DC / RST | `GPIO 5 / 17 / 16` | Chip Select, Data/Command (`TX2`), Reset (`RX2`) |
| **Touch SPI** | T_CS | `GPIO 21` (`D21`) | Dedicated XPT2046 Chip Select (1 MHz SPI) |

---

## 📁 Repository Structure

```
├── platformio.ini               # PlatformIO environment, dependencies & SPI flags
├── train_water_tinyml.py        # Python pipeline: IS 10500 synthesis, MLP training & .tflite export
├── README.md                    # Project documentation
├── SYSTEM_OVERVIEW.md           # Master technical specification & hardware manual
└── src/
    ├── main.cpp                 # Fast 20ms scheduler, dual-core task pinning, mutex sync
    ├── sensors.h / .cpp         # Trimmed-mean eFuse ADC1 filtering, 5V scaling, NVS offsets
    ├── calibration_curves.h     # Verified mathematical transfer equations for all 4 sensors
    ├── water_model.h            # Pre-trained weights, normalization & TFLite byte array
    ├── tinyml_engine.h / .cpp   # 4->16->12->3 MLP inference engine + IS 10500 safety guardrail
    ├── ui_manager.h / .cpp      # 120x86 sprite waveform, 4 parameter cards & touch engine
    ├── status_leds.h / .cpp     # Hardware Status LEDs (GPIO 33 Green, 32 Red, 27 Blue)
    ├── audio_alerts.h / .cpp    # PAM8403 Hardware DAC1 Audio Engine with zero idle hiss
    ├── web_dashboard.h          # SoftAP & Web Dashboard header interface
    ├── web_server.cpp           # Captive Portal & PROGMEM single-page mobile/PC web app
    └── lang_assets.h            # 1-bit monochrome Devanagari typography bitmaps
```

---

## 🚀 Getting Started

### 1. Build and Flash with PlatformIO
```bash
pio run -t upload && pio device monitor
```

### 2. Touch Calibration
- **On Boot:** Hold down the touchscreen during the first **3 seconds** of power-up to calibrate the 4 corners. Calibration values are automatically saved to Flash NVS.
- **On Demand:** Go to `VIEW: RAW` and tap `"RECAL TOUCH"`, or hold the `CAL / ZERO` button for $\ge 2$ seconds.

### 3. Retraining TinyML Model
```bash
python -m pip install tensorflow numpy
python train_water_tinyml.py
```
This generates an updated `water_model.h` containing retrained weights and a quantized `.tflite` model array.

---

## 📜 License
MIT License. Developed for water safety and edge AI research.
