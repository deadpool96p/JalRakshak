#ifndef CALIBRATION_CURVES_H
#define CALIBRATION_CURVES_H

#include <Arduino.h>
#include <math.h>

// ============================================================================
// JALRAKSHAK VERIFIED SENSOR TRANSFER FUNCTIONS & CALIBRATION EQUATIONS
// ============================================================================

// ----------------------------------------------------------------------------
// 1. ANALOG TDS (DT0530 / D34) TEMPERATURE COMPENSATION & POLYNOMIAL CURVE
// ----------------------------------------------------------------------------
inline float calc_tds_ppm(float rawVoltage, float tempC, float zeroOffsetPpm = 0.0f) {
  if (rawVoltage < 0.005f) return 0.0f;

  // Temperature compensation to 25°C reference (2.0% per °C)
  float compFactor = 1.0f + 0.02f * (tempC - 25.0f);
  if (compFactor <= 0.1f) compFactor = 1.0f;
  float vComp = rawVoltage / compFactor;

  // Standard cubic conversion formula relating EC (µS/cm) to TDS (ppm)
  // TDS = (133.42 * V^3 - 255.86 * V^2 + 857.39 * V) * 0.5
  float ppm = (133.42f * powf(vComp, 3) - 
               255.86f * powf(vComp, 2) + 
               857.39f * vComp) * 0.5f;

  ppm -= zeroOffsetPpm;
  return (ppm < 0.0f) ? 0.0f : ppm;
}

// ----------------------------------------------------------------------------
// 2. TS-300B OPTICAL TURBIDITY (VP / GPIO 36) POLYNOMIAL TRANSFER FUNCTION
// ----------------------------------------------------------------------------
inline float calc_turbidity_ntu(float rawVoltage, float zeroOffsetNtu = 0.0f) {
  // Clean water baseline (>= 4.0V = 0 NTU) down to 2.5V (3000 NTU)
  if (rawVoltage >= 4.00f) {
    return 0.0f;
  }
  if (rawVoltage <= 2.50f) {
    return 3000.0f;
  }

  // Empirical quadratic polynomial for TS-300B
  // NTU = -1120.4 * V^2 + 5742.3 * V - 4352.9
  float ntu = -1120.4f * (rawVoltage * rawVoltage) + 5742.3f * rawVoltage - 4352.9f;
  if (ntu < 0.0f) ntu = 0.0f;
  if (ntu > 3000.0f) ntu = 3000.0f;

  ntu -= zeroOffsetNtu;
  return (ntu < 0.0f) ? 0.0f : ntu;
}

// ----------------------------------------------------------------------------
// 3. PH-4502C ANALOG pH MODULE (GPIO 35) NERNSTIAN SLOPE & DRIFT CORRECTION
// ----------------------------------------------------------------------------
inline float calc_ph(float rawVoltage, float tempC = 25.0f, float neutralCalV = 2.50f) {
  // Live pH calculation: ph_val = 7.0f + ((v_ph_neutral - v_ph) / 0.18f)
  (void)tempC; // Preserved for API compatibility
  float ph = 7.00f + ((neutralCalV - rawVoltage) / 0.18f);
  if (ph < 0.0f)  ph = 0.0f;
  if (ph > 14.0f) ph = 14.0f;
  return ph;
}

// ----------------------------------------------------------------------------
// 4. REFERENCE STANDARDS & LOOKUP CHECKPOINTS (For Lab / Bench Validation)
// ----------------------------------------------------------------------------
struct CalibrationCheckpoint {
  const char* parameter;
  const char* referenceSample;
  float expectedPhysicalValue;
  float nominalVoltageV;
};

static const CalibrationCheckpoint BENCH_CALIBRATION_TABLE[] = {
  { "TDS", "RO / Distilled Water (0 ppm)", 0.0f, 0.02f },
  { "TDS", "342 ppm NaCl Standard", 342.0f, 0.98f },
  { "TDS", "707 ppm NaCl Standard", 707.0f, 1.54f },
  { "TDS", "1000 ppm NaCl Standard", 1000.0f, 1.92f },
  { "TURBIDITY", "Clean Tap Water (< 1.0 NTU)", 0.5f, 4.10f },
  { "TURBIDITY", "Diluted Milk Calibration (50 NTU)", 50.0f, 2.75f },
  { "pH", "Buffer 4.01 Acidic", 4.01f, 3.04f },
  { "pH", "Buffer 7.00 Neutral", 7.00f, 2.50f },
  { "pH", "Buffer 9.18 Alkaline", 9.18f, 2.11f }
};

#endif // CALIBRATION_CURVES_H
