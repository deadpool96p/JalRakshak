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
static float g_vPhNeutral = PH_NEUTRAL_CAL_DEFAULT; // 2.50f default neutral reference
static float g_simFallbackTempC = 25.0f;

// 18650 Battery State
static bool g_lowBatAlertFired = false;

float sensors_get_tds_offset() {
  return g_tdsZeroOffset;
}

float sensors_get_turb_offset() {
  return g_turbZeroOffset;
}

float sensors_get_ph_neutral() {
  return g_vPhNeutral;
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
    g_vPhNeutral = prefs.getFloat("phNeut", PH_NEUTRAL_CAL_DEFAULT);
    prefs.end();
    Serial.printf("[NVS] Loaded Sensor Offsets: TDS=%.1f ppm, Turb=%.2f NTU, pH Neutral=%.3f V\n",
                  g_tdsZeroOffset, g_turbZeroOffset, g_vPhNeutral);
  }
}

void sensors_save_offsets_to_nvs() {
  Preferences prefs;
  if (prefs.begin("jalrakshak", false)) { // read-write mode
    prefs.putFloat("tdsOff", g_tdsZeroOffset);
    prefs.putFloat("turbOff", g_turbZeroOffset);
    prefs.putFloat("phNeut", g_vPhNeutral);
    prefs.end();
    Serial.printf("[NVS] Stored Sensor Offsets: TDS=%.1f ppm, Turb=%.2f NTU, pH Neutral=%.3f V\n",
                  g_tdsZeroOffset, g_turbZeroOffset, g_vPhNeutral);
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================
void sensors_init() {
  analogReadResolution(12); // 12-bit ADC (0 - 4095)
  analogSetPinAttenuation(PIN_TDS, ADC_11db);
  analogSetPinAttenuation(PIN_TURBIDITY, ADC_11db);
  analogSetPinAttenuation(PIN_PH, ADC_11db);
  analogSetPinAttenuation(PIN_BATTERY, ADC_11db);

  tempSensor.begin();
  tempSensor.setWaitForConversion(false); // Non-blocking temperature requests

  pinMode(PIN_TDS, INPUT);
  pinMode(PIN_TURBIDITY, INPUT);
  pinMode(PIN_PH, INPUT);
  pinMode(PIN_BATTERY, INPUT);

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
    telemetry.pinTdsV = 2.45f;
    telemetry.rawTdsV = 2.45f;
    telemetry.tdsPpm = 680.0f;       // High TDS spike (> 500 IS 10500 limit)
    telemetry.pinTurbV = 1.23f;
    telemetry.rawTurbV = 1.85f;
    telemetry.turbNTU = 18.0f;       // High Turbidity (> 5 IS 10500 limit)
    telemetry.pinPhV = 1.95f;
    telemetry.rawPhV = 2.92f;
    telemetry.pH = 5.40f;            // Acidic contamination (< 6.5 IS 10500 limit)
    telemetry.turbStandby = false;
    telemetry.phStandby = false;
    return;
  }

  // --------------------------------------------------------------------------
  // 2. TEMPERATURE (DS18B20 1-Wire on GPIO 4 with Live Compensation)
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
  // 3. ANALOG TDS (GPIO 34 / ADC1_CH6 - 3.3V Direct, Ratio = 1.0f)
  // --------------------------------------------------------------------------
  float pinTdsV = sensors_read_adc_voltage(PIN_TDS, 32);
  telemetry.pinTdsV = pinTdsV;
  float v_sensor_tds = pinTdsV * TDS_DIVIDER_RATIO;
  telemetry.rawTdsV = v_sensor_tds;

  // Temperature compensation to 25°C reference (2.0% per °C)
  float compFactor = 1.0f + 0.02f * (telemetry.tempC - 25.0f);
  if (compFactor <= 0.1f) compFactor = 1.0f;
  float v_comp = v_sensor_tds / compFactor;

  // Cubic transfer polynomial
  float tds_ppm = (133.42f * powf(v_comp, 3) - 
                   255.86f * powf(v_comp, 2) + 
                   857.39f * v_comp) * 0.5f;
  tds_ppm -= g_tdsZeroOffset;
  if (tds_ppm < 0.0f) tds_ppm = 0.0f;
  telemetry.tdsPpm = tds_ppm;

  // --------------------------------------------------------------------------
  // 4. ANALOG TURBIDITY (GPIO 36 / VP / ADC1_CH0 - 5.0V with 10k/20k Divider, Ratio = 1.50f)
  // --------------------------------------------------------------------------
  float pinTurbV = sensors_read_adc_voltage(PIN_TURBIDITY, 32);
  telemetry.pinTurbV = pinTurbV;
  float v_turb = pinTurbV * TURB_DIVIDER_RATIO;
  telemetry.rawTurbV = v_turb;
  telemetry.turbStandby = false; // No strict standby freeze

  // Map v_turb from clean water baseline (>= 4.0f V = 0 NTU) down to 2.5f V (3000 NTU)
  telemetry.turbNTU = calc_turbidity_ntu(v_turb, g_turbZeroOffset);

  // --------------------------------------------------------------------------
  // 5. ANALOG pH (GPIO 35 / D35 / ADC1_CH7 - 5.0V with 10k/20k Divider, Ratio = 1.50f)
  // --------------------------------------------------------------------------
  float pinPhV = sensors_read_adc_voltage(PIN_PH, 32);
  telemetry.pinPhV = pinPhV;
  float v_ph = pinPhV * PH_DIVIDER_RATIO;
  telemetry.rawPhV = v_ph;
  telemetry.phStandby = false; // Live fluctuating values

  // Calculate live pH: ph_val = 7.0f + ((v_ph_neutral - v_ph) / 0.18f)
  telemetry.pH = calc_ph(v_ph, telemetry.tempC, g_vPhNeutral);

  // --------------------------------------------------------------------------
  // 6. 18650 BATTERY MONITOR (GPIO 39 / VN / ADC1_CH3 - 100k/100k Divider, Ratio = 2.00f)
  // --------------------------------------------------------------------------
  float pinBatV = sensors_read_adc_voltage(PIN_BATTERY, 32);
  telemetry.rawBatPinV = pinBatV;
  float v_bat = pinBatV * BAT_DIVIDER_RATIO + BAT_CAL_OFFSET;
  telemetry.batVolts = v_bat;

  // Map 3.20V -> 0% to 4.20V -> 100% (clamped 0–100%)
  float bat_pct = (v_bat - BAT_VOLTS_MIN) / (BAT_VOLTS_MAX - BAT_VOLTS_MIN) * 100.0f;
  if (bat_pct < 0.0f)   bat_pct = 0.0f;
  if (bat_pct > 100.0f) bat_pct = 100.0f;

  if (v_bat < BAT_MIN_DETECT_V) {
    // Battery disconnected / running purely on USB 5V
    telemetry.batConnected = false;
    telemetry.batPercent = 100.0f;
    telemetry.lowBatAlertTriggered = false;
  } else {
    telemetry.batConnected = true;
    telemetry.batPercent = bat_pct;

    // Low-Battery (< 15%) Alert Signal Guard
    if (telemetry.batPercent < 15.0f) {
      if (!g_lowBatAlertFired) {
        g_lowBatAlertFired = true;
        telemetry.lowBatAlertTriggered = true;
      } else {
        telemetry.lowBatAlertTriggered = false;
      }
    } else {
      telemetry.lowBatAlertTriggered = false;
      if (telemetry.batPercent >= 20.0f) {
        g_lowBatAlertFired = false; // Reset hysteresis
      }
    }
  }
}

// ============================================================================
// ZERO CALIBRATION (Sets Clean Tap Water Offset & Persists to NVS)
// ============================================================================
void sensors_calibrate_zero(WaterTelemetry &telemetry) {
  // Store un-offset physical metrics as zero offsets
  float rawPpm = calc_tds_ppm(telemetry.rawTdsV, telemetry.tempC, 0.0f);
  g_tdsZeroOffset = rawPpm;
  telemetry.tdsOffset = g_tdsZeroOffset;
  telemetry.tdsPpm = 0.0f;

  g_turbZeroOffset = 0.0f;
  telemetry.turbOffset = 0.0f;
  telemetry.turbNTU = 0.0f;

  // Update pH neutral voltage calibration to current rawPhV
  g_vPhNeutral = telemetry.rawPhV;
  telemetry.pH = 7.00f;

  sensors_save_offsets_to_nvs();
  Serial.printf("[NVS] Zero Calibrated: TDS Offset=%.1f ppm, pH Neutral=%.3f V\n",
                g_tdsZeroOffset, g_vPhNeutral);
}

// ============================================================================
// RESET CALIBRATION OFFSETS (Clears Baseline Offsets & Persists to NVS)
// ============================================================================
void sensors_reset_offsets(WaterTelemetry &telemetry) {
  g_tdsZeroOffset = 0.0f;
  g_turbZeroOffset = 0.0f;
  g_vPhNeutral = PH_NEUTRAL_CAL_DEFAULT;
  telemetry.tdsOffset = 0.0f;
  telemetry.turbOffset = 0.0f;

  sensors_save_offsets_to_nvs();
  Serial.println("[NVS] Sensor Offsets Cleared to Defaults (pH Neutral = 2.50V)");
}
