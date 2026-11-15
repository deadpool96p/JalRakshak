#include "sensors.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>

// 1-Wire & DallasTemperature Instances
static OneWire oneWire(PIN_TEMP_DS18B20);
static DallasTemperature tempSensor(&oneWire);

// Persistent Calibration Offsets
static float g_tdsZeroOffset = 0.0f;
static float g_turbZeroOffset = 0.0f;
static float g_simFallbackTempC = 25.0f;

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
}

// ============================================================================
// ADC MULTI-SAMPLE AVERAGING (Filters ESP32 ADC noise)
// ============================================================================
float sensors_read_adc_voltage(int pin, int samples) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(50);
  }
  float avgAdc = (float)sum / (float)samples;
  // ESP32 calibrated 3.3V reference mapping
  return (avgAdc / 4095.0f) * 3.3f;
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
  // 3. ANALOG TDS (Temperature-Compensated Polynomial)
  // --------------------------------------------------------------------------
  telemetry.rawTdsV = sensors_read_adc_voltage(PIN_TDS, 32);
  // Temperature compensation formula (2%/°C standard reference at 25°C)
  float compFactor = 1.0f + 0.02f * (telemetry.tempC - 25.0f);
  float compVoltage = telemetry.rawTdsV / compFactor;

  // Standard TDS curve mapping: 0.5 factor relates EC (µS/cm) to TDS (ppm)
  float rawPpm = (133.42f * powf(compVoltage, 3) - 
                  255.86f * powf(compVoltage, 2) + 
                  857.39f * compVoltage) * 0.5f;
  
  rawPpm -= g_tdsZeroOffset;
  if (rawPpm < 0.0f) rawPpm = 0.0f;
  telemetry.tdsPpm = rawPpm;

  // --------------------------------------------------------------------------
  // 4. ANALOG TURBIDITY (TS-300B Sensor Curve)
  // --------------------------------------------------------------------------
  telemetry.rawTurbV = sensors_read_adc_voltage(PIN_TURBIDITY, 32);
  float tV = telemetry.rawTurbV;

  // TS-300B Voltage to NTU Transfer Equation:
  // Clean water gives ~3.5V to 4.2V (low NTU). Cloudy water gives lower voltage.
  float ntu = 0.0f;
  if (tV >= 3.80f) {
    ntu = 0.5f; // Crystal clear drinking water
  } else if (tV <= 2.20f) {
    ntu = 100.0f; // Extremely turbid / mud
  } else {
    // Polynomial characteristic curve for TS-300B
    ntu = -1120.4f * (tV * tV) + 5742.3f * tV - 4353.8f;
    if (ntu < 0.0f) ntu = 0.0f;
  }

  ntu -= g_turbZeroOffset;
  if (ntu < 0.0f) ntu = 0.0f;
  telemetry.turbNTU = ntu;

  // --------------------------------------------------------------------------
  // 5. ANALOG pH (D35 Po Pin or Neutral 7.10 Fallback)
  // --------------------------------------------------------------------------
  telemetry.rawPhV = sensors_read_adc_voltage(PIN_PH, 32);
#if PH_SENSOR_CONNECTED
  // Standard pH probe transfer curve: 2.5V is pH 7.0 neutral, 0.18V per pH unit
  float calculatedPh = 7.0f + ((2.50f - telemetry.rawPhV) / 0.18f);
  if (calculatedPh < 0.0f)  calculatedPh = 0.0f;
  if (calculatedPh > 14.0f) calculatedPh = 14.0f;
  telemetry.pH = calculatedPh;
#else
  // Default to neutral drinking water baseline with subtle analog noise realism
  telemetry.pH = PH_DEFAULT_VALUE;
#endif
}

// ============================================================================
// ZERO CALIBRATION (Sets Clean Tap Water Offset)
// ============================================================================
void sensors_calibrate_zero(WaterTelemetry &telemetry) {
  g_tdsZeroOffset = telemetry.tdsPpm;
  g_turbZeroOffset = telemetry.turbNTU;
  telemetry.tdsOffset = g_tdsZeroOffset;
  telemetry.turbOffset = g_turbZeroOffset;
  telemetry.tdsPpm = 0.0f;
  telemetry.turbNTU = 0.0f;
}
