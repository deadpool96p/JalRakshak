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

// ============================================================================
// SENSOR TOGGLES & CONSTANTS
// ============================================================================
#define PH_SENSOR_CONNECTED false   // Set to true when physical pH probe is attached
#define PH_DEFAULT_VALUE    7.10f   // Baseline neutral pH when probe toggle is false

// Voltage Divider Ratios (10k + 20k resistor divider to scale 5.0V max output down to 3.3V ESP32 ADC)
// Scale Factor = (10k + 20k) / 20k = 1.50x
#define TURB_VOLTAGE_SCALE  1.50f   // Multiplier for TS-300B Turbidity divider on VP (GPIO 36)
#define PH_VOLTAGE_SCALE    1.50f   // Multiplier for PH-4502C pH divider on D35 (GPIO 35)

// Unplugged / Floating Sensor Guard Voltage Threshold (measured at physical ESP32 pin before scaling)
// A disconnected wire or un-submerged probe reading < 0.25V is placed in safe Standby mode
#define SENSOR_UNPLUGGED_VOLTAGE_THRESHOLD 0.25f

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

  // Raw Diagnostic Values
  float rawTdsV;
  float rawTurbV;
  float rawPhV;
  bool  tempConnected;
  bool  turbStandby;        // True if VP raw voltage < 0.25V (unplugged/standby)
  bool  phStandby;          // True if D35 raw voltage < 0.25V or PH_SENSOR_CONNECTED is false
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
float sensors_read_adc_voltage(int pin, int samples = 32);
void sensors_cycle_sim_temp();
float sensors_get_sim_temp();

#endif // SENSORS_H
