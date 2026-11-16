#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

// ============================================================================
// HARDWARE PIN DEFINITIONS (STRICT)
// ============================================================================
#define PIN_TEMP_DS18B20  4    // D4:  DS18B20 1-Wire Waterproof Temperature Probe
#define PIN_TDS           34   // D34: Analog TDS Sensor (ADC1 Channel 6)
#define PIN_TURBIDITY     36   // VP:  Analog Turbidity Sensor TS-300B (ADC1 Channel 0)
#define PIN_PH            35   // D35: Analog pH Sensor Po Pin (ADC1 Channel 7)
#define PIN_BATTERY       39   // VN:  Analog 18650 Battery Voltage (ADC1 Channel 3)

// ============================================================================
// SENSOR TOGGLES & CONSTANTS
// ============================================================================
#define PH_SENSOR_CONNECTED true    // Physical pH probe enabled
#define PH_DEFAULT_VALUE    7.00f   // Baseline neutral pH
#define PH_NEUTRAL_CAL_DEFAULT 2.50f // Default neutral voltage (reconstructed 5V module output)

// Voltage Divider Ratios
#define TDS_DIVIDER_RATIO   1.00f   // 3.3V direct, no divider on D34 (GPIO 34)
#define PH_DIVIDER_RATIO    1.50f   // 10k + 20k resistor divider on D35 (GPIO 35)
#define TURB_DIVIDER_RATIO  1.50f   // 10k + 20k resistor divider on VP (GPIO 36)
#define BAT_DIVIDER_RATIO   2.00f   // 100k + 100k resistor divider on VN (GPIO 39)

// Compatibility aliases
#define TURB_VOLTAGE_SCALE  TURB_DIVIDER_RATIO
#define PH_VOLTAGE_SCALE    PH_DIVIDER_RATIO

// 18650 Battery Monitor Constants
#define BAT_CAL_OFFSET      0.00f   // Benchmark calibration voltage offset
#define BAT_MIN_DETECT_V    2.60f   // Threshold below which device is running on USB power (switch OFF)
#define BAT_VOLTS_MIN       3.20f   // 3.20V -> 0%
#define BAT_VOLTS_MAX       4.20f   // 4.20V -> 100%

// Calibration and IS 10500 Safety Limits
#define IS10500_PH_MIN        6.50f
#define IS10500_PH_MAX        8.50f
#define IS10500_TDS_MAX       500.0f   // Acceptable limit (Permissible without alternate source: 2000)
#define IS10500_TURB_MAX      5.00f    // Acceptable limit in NTU (Ideal: < 1.0 NTU)

// ============================================================================
// TELEMETRY DATA STRUCTURE (Thread-Safe Shared Memory)
// ============================================================================
struct WaterTelemetry {
  // Physical Calibrated Metrics
  float tempC;
  float tdsPpm;
  float turbNTU;
  float pH;

  // 18650 Battery Monitor Metrics
  float batVolts;
  float batPercent;
  bool  batConnected;
  bool  lowBatAlertTriggered;

  // Raw Diagnostic Values (both physical pin voltage and reconstructed sensor voltage)
  float pinTdsV;
  float pinTurbV;
  float pinPhV;
  float rawBatPinV;
  float rawTdsV;            // Reconstructed sensor voltage (pinTdsV * TDS_DIVIDER_RATIO)
  float rawTurbV;           // Reconstructed sensor voltage (pinTurbV * TURB_DIVIDER_RATIO)
  float rawPhV;             // Reconstructed sensor voltage (pinPhV * PH_DIVIDER_RATIO)
  bool  tempConnected;
  bool  turbStandby;        // Standby flag
  bool  phStandby;          // Standby flag
  bool  isSimSpike;

  // TinyML Edge AI Outputs
  float probSafe;
  float probCaution;
  float probContaminated;
  int   predictedClass;    // 0 = SAFE, 1 = CAUTION, 2 = CONTAMINATED
  uint32_t inferenceTimeUs; // Latency in microseconds

  // Hybrid Fusion Status (TinyML + Deterministic IS 10500 Fail-Safe)
  int   finalStatus;       // 0 = SAFE, 1 = CAUTION, 2 = CONTAMINATED
  bool  failSafeTriggered;
  char  alertReason[48];

  // Calibration Offsets (Persisted in NVS)
  float tdsOffset;
  float turbOffset;

  uint32_t sampleCount;
  uint32_t timestampMs;
};

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================
void sensors_init();
void sensors_read(WaterTelemetry &telemetry, bool simSpikeActive);
void sensors_calibrate_zero(WaterTelemetry &telemetry);
void sensors_reset_offsets(WaterTelemetry &telemetry);
void sensors_load_offsets_from_nvs();
void sensors_save_offsets_to_nvs();
float sensors_get_tds_offset();
float sensors_get_turb_offset();
float sensors_get_ph_neutral();
float sensors_read_adc_voltage(int pin, int samples = 32);
float sensors_calc_bat_percent(float volts);
void sensors_cycle_sim_temp();
float sensors_get_sim_temp();

#endif // SENSORS_H
