# 💧 JalRakshak: Edge AI Offline-First Water Quality Monitoring Node

**JalRakshak** is an offline-first, embedded water quality diagnostic node powered by an **ESP32-WROOM-32** microcontroller, a **2.8" ILI9341 SPI TFT touchscreen** with an **XPT2046** resistive touch controller, and a local **TinyML Neural Network** running real-time anomaly detection backed by deterministic **IS 10500** drinking water safety guardrails.

---

## 🌟 Key Features

- **FreeRTOS Dual-Core Architecture:**
  - **Core 0:** Multi-sample ADC1 filtering (32-sample averaging), temperature-compensated TDS, TS-300B turbidity polynomial modeling, and 4→16→12→3 TinyML anomaly inference ($< 350\text{ }\mu\text{s}$ latency).
  - **Core 1:** Smooth $\approx 30\text{ FPS}$ flicker-free UI rendering, compact sprite animations, and edge-triggered touch processing.
- **Local TinyML Anomaly Detection:**
  - 4-feature normalized input: $[pH, TDS, Turbidity, Temperature]$.
  - Output softmax classification: `0 = SAFE`, `1 = CAUTION / DRIFT`, `2 = CONTAMINATED`.
  - Fail-safe hybrid fusion with Indian Standard **IS 10500** limits ($pH \in [6.5, 8.5]$, $TDS < 500\text{ ppm}$, $Turbidity < 5\text{ NTU}$).
- **Interactive 2.8" ILI9341 Touchscreen UI:**
  - **Multi-Screen State Machine:**
    1. `VIEW: DASH`: 4 parameter cards + live animated contamination risk probability waveform sprite ($120 \times 86\text{ px}$, $20.6\text{ KB SRAM}$).
    2. `VIEW: RAW`: Live raw sensor voltages, temperature compensation factor, active baseline offsets, and hardware settings.
    3. `VIEW: ML`: Large contamination risk gauge bar ($0\text{--}100\%$), per-class softmax distribution bars, and guardrail compliance breakdown.
  - **Multilingual Support:** English, Hindi (हिंदी), and Marathi (मराठी) with pixel-perfect 1-bit monochrome Devanagari typography.
  - **Hardware PWM Backlight:** 3 brightness presets on `GPIO 22` (`LOW 30%`, `MED 65%`, `HIGH 100%`) saved in Flash NVS.
  - **Touch Calibration Persistence:** NVS Flash persistence via `<Preferences.h>` with 3-second boot hold trigger and on-demand recalibration.

---

## 📌 Hardware Pin Mapping

| Subsystem | Signal / Sensor | ESP32 Pin | Details |
| :--- | :--- | :--- | :--- |
| **Temperature** | DS18B20 Probe | `GPIO 4` (`D4`) | 1-Wire Data with 4.7kΩ pull-up ($25.0^\circ\text{C}$ fallback) |
| **TDS Sensor** | Analog TDS Signal | `GPIO 34` (`D34`) | ADC1 Ch6, temperature-compensated |
| **Turbidity** | TS-300B Analog Out | `GPIO 36` (`VP`) | ADC1 Ch0, polynomial mapped to NTU |
| **pH Sensor** | Analog pH `Po` Pin | `GPIO 35` (`D35`) | ADC1 Ch7 (`PH_SENSOR_CONNECTED` toggle) |
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
└── src/
    ├── main.cpp                 # FreeRTOS setup, task pinning, mutex synchronization
    ├── sensors.h / .cpp         # Multi-sample filtering, temperature compensation, calibration
    ├── water_model.h            # Pre-trained weights, normalization & TFLite byte array
    ├── tinyml_engine.h / .cpp   # 4->16->12->3 MLP inference engine + IS 10500 safety guardrail
    ├── ui_manager.h / .cpp      # 120x86 sprite waveform, 4 parameter cards & touch engine
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
