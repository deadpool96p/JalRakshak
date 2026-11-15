#include "sensors.h"
#include "calibration_curves.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <math.h>

// 1-Wire & DallasTemperature Instances
static OneWire oneWire(PIN_TEMP_DS18B20);
static DallasTemperature tempSensor(&oneWire);

// Persistent Calibration Offsets
static float g_tdsZeroOffset = 0.0f;
static float g_turbZeroOffset = 0.0f;
static float g_simFallbackTempC = 25.0f;

float sensors_get_tds_offset() {
  return g_tdsZeroOffset;
}

float sensors_get_turb_offset() {
  return g_turbZeroOffset;
}

void sensors_cycle_sim_temp() {
  if (g_simFallbackTempC == 20.0f) g_simFallbackTempC = 25.0f;
  else if (g_simFallbackTempC == 25.0f) g_simFallbackTempC = 30.0f;
  else if (g_simFallbackTempC == 30.0f) g_simFallbackTempC = 35.0f;
  else g_simFallbackTempC = 20.0f;
}

float sensors_get_sim_temp() {
  return g_simFallbackTempC;
}

// ============================================================================
// NVS FLASH PERSISTENCE FOR CALIBRATION OFFSETS
// ============================================================================
void sensors_load_offsets_from_nvs() {
  Preferences prefs;
  if (prefs.begin("jalrakshak", true)) { // read-only mode
    g_tdsZeroOffset = prefs.getFloat("tdsOff", 0.0f);
    g_turbZeroOffset = prefs.getFloat("turbOff", 0.0f);
    prefs.end();
    Serial.printf("[NVS] Loaded Sensor Offsets: TDS=%.1f ppm, Turb=%.2f NTU\n", g_tdsZeroOffset, g_turbZeroOffset);
  }
}

void sensors_save_offsets_to_nvs() {
  Preferences prefs;
  if (prefs.begin("jalrakshak", false)) { // read-write mode
    prefs.putFloat("tdsOff", g_tdsZeroOffset);
    prefs.putFloat("turbOff", g_turbZeroOffset);
    prefs.end();
    Serial.printf("[NVS] Stored Sensor Offsets: TDS=%.1f ppm, Turb=%.2f NTU\n", g_tdsZeroOffset, g_turbZeroOffset);
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================
void sensors_init() {
  analogReadResolution(12); // 12-bit ADC (0 - 4095)
  analogSetAttenuation(ADC_11db); // Full range up to ~3.3V

  tempSensor.begin();
  tempSensor.setWaitForConversion(false); // Non-blocking temperature requests

  pinMode(PIN_TDS, INPUT);
  pinMode(PIN_TURBIDITY, INPUT);
  pinMode(PIN_PH, INPUT);

  sensors_load_offsets_from_nvs();
}

// ============================================================================
// ADC MULTI-SAMPLE FILTERING (32-sample Trimmed Mean with eFuse Millivolt Calibration)
// Discards 4 lowest and 4 highest outliers to eliminate noise & electrochemical crosstalk
// ============================================================================
float sensors_read_adc_voltage(int pin, int samples) {
  if (samples < 16) samples = 32;
  const int maxSamples = 64;
  int count = (samples <= maxSamples) ? samples : maxSamples;
  uint32_t rawMv[maxSamples];

  for (int i = 0; i < count; i++) {
    rawMv[i] = analogReadMilliVolts(pin);
    delayMicroseconds(50);
  }

  // Insertion sort for outlier rejection
  for (int i = 1; i < count; i++) {
    uint32_t key = rawMv[i];
    int j = i - 1;
    while (j >= 0 && rawMv[j] > key) {
      rawMv[j + 1] = rawMv[j];
      j--;
    }
    rawMv[j + 1] = key;
  }

  // Discard 4 lowest and 4 highest outliers
  int discard = 4;
  int startIdx = discard;
  int endIdx = count - discard;
  if (endIdx <= startIdx) {
    startIdx = 0;
    endIdx = count;
  }

  uint32_t sum = 0;
  for (int i = startIdx; i < endIdx; i++) {
    sum += rawMv[i];
  }

  float avgMv = (float)sum / (float)(endIdx - startIdx);
  return avgMv / 1000.0f; // Return calibrated voltage in Volts
}

// ============================================================================
// SAMPLE ALL SENSORS & CONVERT TO CALIBRATED METRICS
// ============================================================================
void sensors_read(WaterTelemetry &telemetry, bool simSpikeActive) {
  telemetry.isSimSpike = simSpikeActive;
  telemetry.tdsOffset = g_tdsZeroOffset;
  telemetry.turbOffset = g_turbZeroOffset;
  telemetry.sampleCount++;
  telemetry.timestampMs = millis();

  // --------------------------------------------------------------------------
  // 1. SIMULATION SPIKE OVERRIDE (For Live Demos & Jury Verification)
  // --------------------------------------------------------------------------
  if (simSpikeActive) {
    telemetry.tempC = 25.0f;
    telemetry.tempConnected = true;
    telemetry.rawTdsV = 2.45f;
    telemetry.tdsPpm = 680.0f;       // High TDS spike (> 500 IS 10500 limit)
    telemetry.rawTurbV = 1.85f;
    telemetry.turbNTU = 18.0f;       // High Turbidity (> 5 IS 10500 limit)
    telemetry.rawPhV = 2.92f;
    telemetry.pH = 5.40f;            // Acidic contamination (< 6.5 IS 10500 limit)
    telemetry.turbStandby = false;
    telemetry.phStandby = false;
    return;
  }

  // --------------------------------------------------------------------------
  // 2. TEMPERATURE (DS18B20 1-Wire with 25.0°C Fallback)
  // --------------------------------------------------------------------------
  tempSensor.requestTemperatures();
  float rawTemp = tempSensor.getTempCByIndex(0);
  if (rawTemp <= -55.0f || rawTemp >= 125.0f || rawTemp == DEVICE_DISCONNECTED_C) {
    telemetry.tempConnected = false;
    telemetry.tempC = g_simFallbackTempC; // Dynamic simulated fallback
  } else {
    telemetry.tempConnected = true;
    telemetry.tempC = rawTemp;
  }

  // --------------------------------------------------------------------------
  // 3. ANALOG TDS (3.3V Direct, Temperature-Compensated Polynomial)
  // --------------------------------------------------------------------------
  telemetry.rawTdsV = sensors_read_adc_voltage(PIN_TDS, 32);
  telemetry.tdsPpm = calc_tds_ppm(telemetry.rawTdsV, telemetry.tempC, g_tdsZeroOffset);

  // --------------------------------------------------------------------------
  // 4. ANALOG TURBIDITY (TS-300B with 10k+20k Divider Scaling & Standby Guard)
  // --------------------------------------------------------------------------
  float pinTurbV = sensors_read_adc_voltage(PIN_TURBIDITY, 32);
  if (pinTurbV < SENSOR_UNPLUGGED_VOLTAGE_THRESHOLD) {
    // Unplugged / floating wire guard: standby mode prevents false 100+ NTU alarm
    telemetry.turbStandby = true;
    telemetry.rawTurbV = pinTurbV * TURB_VOLTAGE_SCALE;
    telemetry.turbNTU = 0.8f; // Safe baseline in standby
  } else {
    telemetry.turbStandby = false;
    telemetry.rawTurbV = pinTurbV * TURB_VOLTAGE_SCALE;
    telemetry.turbNTU = calc_turbidity_ntu(telemetry.rawTurbV, g_turbZeroOffset);
  }

  // --------------------------------------------------------------------------
  // 5. ANALOG pH (PH-4502C with 10k+20k Divider Scaling & Standby Guard)
  // --------------------------------------------------------------------------
  float pinPhV = sensors_read_adc_voltage(PIN_PH, 32);
#if PH_SENSOR_CONNECTED
  if (pinPhV < SENSOR_UNPLUGGED_VOLTAGE_THRESHOLD) {
    telemetry.phStandby = true;
    telemetry.rawPhV = pinPhV * PH_VOLTAGE_SCALE;
    telemetry.pH = PH_DEFAULT_VALUE;
  } else {
    telemetry.phStandby = false;
    telemetry.rawPhV = pinPhV * PH_VOLTAGE_SCALE;
    telemetry.pH = calc_ph(telemetry.rawPhV, telemetry.tempC);
  }
#else
  // Hardware toggle false: treat as standby baseline
  telemetry.phStandby = true;
  telemetry.rawPhV = pinPhV * PH_VOLTAGE_SCALE;
  telemetry.pH = PH_DEFAULT_VALUE;
#endif
}

// ============================================================================
// ZERO CALIBRATION (Sets Clean Tap Water Offset & Persists to NVS)
// ============================================================================
void sensors_calibrate_zero(WaterTelemetry &telemetry) {
  // Store un-offset physical metrics as zero offsets
  float rawPpm = calc_tds_ppm(telemetry.rawTdsV, telemetry.tempC, 0.0f);
  float rawNtu = calc_turbidity_ntu(telemetry.rawTurbV, 0.0f);

  g_tdsZeroOffset = rawPpm;
  g_turbZeroOffset = rawNtu;
  telemetry.tdsOffset = g_tdsZeroOffset;
  telemetry.turbOffset = g_turbZeroOffset;
  telemetry.tdsPpm = 0.0f;
  telemetry.turbNTU = 0.0f;

  sensors_save_offsets_to_nvs();
}

// ============================================================================
// RESET CALIBRATION OFFSETS (Clears Baseline Offsets & Persists to NVS)
// ============================================================================
void sensors_reset_offsets(WaterTelemetry &telemetry) {
  g_tdsZeroOffset = 0.0f;
  g_turbZeroOffset = 0.0f;
  telemetry.tdsOffset = 0.0f;
  telemetry.turbOffset = 0.0f;

  sensors_save_offsets_to_nvs();
  Serial.println("[NVS] Sensor Offsets Cleared to 0.0");
}
