#include "tinyml_engine.h"
#include "water_model.h"
#include <math.h>
#include <string.h>

// ============================================================================
// INITIALIZATION
// ============================================================================
void tinyml_init() {
  // Pre-warmed state for zero-latency initial inference
}

// ============================================================================
// FORWARD INFERENCE PASS & HYBRID IS 10500 FUSION
// ============================================================================
void tinyml_infer(WaterTelemetry &telemetry) {
  uint32_t tStart = micros();

  // 1. Feature Normalization
  float norm[4];
  norm[0] = (telemetry.pH - NORM_PH_MEAN) / NORM_PH_STD;
  norm[1] = (telemetry.tdsPpm - NORM_TDS_MEAN) / NORM_TDS_STD;
  norm[2] = (telemetry.turbNTU - NORM_TURB_MEAN) / NORM_TURB_STD;
  norm[3] = (telemetry.tempC - NORM_TEMP_MEAN) / NORM_TEMP_STD;

  // 2. Layer 1: Dense(4, 16) + ReLU
  float h1[16];
  for (int j = 0; j < 16; j++) {
    float sum = LAYER1_B[j];
    for (int i = 0; i < 4; i++) {
      sum += norm[i] * LAYER1_W[i][j];
    }
    h1[j] = (sum > 0.0f) ? sum : 0.0f; // ReLU
  }

  // 3. Layer 2: Dense(16, 12) + ReLU
  float h2[12];
  for (int j = 0; j < 12; j++) {
    float sum = LAYER2_B[j];
    for (int i = 0; i < 16; i++) {
      sum += h1[i] * LAYER2_W[i][j];
    }
    h2[j] = (sum > 0.0f) ? sum : 0.0f; // ReLU
  }

  // 4. Layer 3: Dense(12, 3) Logits
  float logits[3];
  float maxLogit = -1e9f;
  for (int k = 0; k < 3; k++) {
    float sum = LAYER3_B[k];
    for (int i = 0; i < 12; i++) {
      sum += h2[i] * LAYER3_W[i][k];
    }
    logits[k] = sum;
    if (sum > maxLogit) maxLogit = sum;
  }

  // 5. Softmax Normalization
  float expSum = 0.0f;
  float exps[3];
  for (int k = 0; k < 3; k++) {
    exps[k] = expf(logits[k] - maxLogit);
    expSum += exps[k];
  }

  telemetry.probSafe         = exps[CLASS_SAFE] / expSum;
  telemetry.probCaution      = exps[CLASS_CAUTION] / expSum;
  telemetry.probContaminated = exps[CLASS_CONTAMINATED] / expSum;

  // Argmax classification
  if (telemetry.probContaminated >= telemetry.probCaution && 
      telemetry.probContaminated >= telemetry.probSafe) {
    telemetry.predictedClass = CLASS_CONTAMINATED;
  } else if (telemetry.probCaution >= telemetry.probSafe) {
    telemetry.predictedClass = CLASS_CAUTION;
  } else {
    telemetry.predictedClass = CLASS_SAFE;
  }

  telemetry.inferenceTimeUs = micros() - tStart;

  // ==========================================================================
  // DETERMINISTIC IS 10500 FAIL-SAFE GUARDRAILS (Zero False-Negative Safety)
  // ==========================================================================
  bool breach = false;

  if (telemetry.tdsPpm > IS10500_TDS_MAX) {
    breach = true;
    snprintf(telemetry.alertReason, sizeof(telemetry.alertReason), "TDS > 500 ppm (IS 10500)");
  } else if (telemetry.turbNTU > IS10500_TURB_MAX) {
    breach = true;
    snprintf(telemetry.alertReason, sizeof(telemetry.alertReason), "Turbidity > 5 NTU (IS 10500)");
  } else if (telemetry.pH < IS10500_PH_MIN) {
    breach = true;
    snprintf(telemetry.alertReason, sizeof(telemetry.alertReason), "pH < 6.5 (Acidic Risk)");
  } else if (telemetry.pH > IS10500_PH_MAX) {
    breach = true;
    snprintf(telemetry.alertReason, sizeof(telemetry.alertReason), "pH > 8.5 (Alkaline Risk)");
  }

  if (breach) {
    telemetry.failSafeTriggered = true;
    // Critical breach escalates directly to CONTAMINATED
    if (telemetry.tdsPpm > 600.0f || telemetry.turbNTU > 10.0f || 
        telemetry.pH < 6.0f || telemetry.pH > 9.0f || 
        telemetry.predictedClass == CLASS_CONTAMINATED) {
      telemetry.finalStatus = CLASS_CONTAMINATED;
    } else {
      telemetry.finalStatus = CLASS_CAUTION;
    }

    // Synchronize P(Contam) and scrolling waveform with Guardrail Overrides
    float mlContamProb = telemetry.probContaminated;
    if (telemetry.finalStatus == CLASS_CONTAMINATED) {
      // Calculate severity of breach beyond IS 10500 limits
      float severity = 0.0f;
      if (telemetry.tdsPpm > IS10500_TDS_MAX) {
        float s = (telemetry.tdsPpm - IS10500_TDS_MAX) / 500.0f; // reaches 1.0 at 1000 ppm
        if (s > severity) severity = s;
      }
      if (telemetry.turbNTU > IS10500_TURB_MAX) {
        float s = (telemetry.turbNTU - IS10500_TURB_MAX) / 25.0f;   // reaches 1.0 at 30 NTU
        if (s > severity) severity = s;
      }
      if (telemetry.pH < IS10500_PH_MIN) {
        float s = (IS10500_PH_MIN - telemetry.pH) / 2.0f;         // reaches 1.0 at pH 4.5
        if (s > severity) severity = s;
      } else if (telemetry.pH > IS10500_PH_MAX) {
        float s = (telemetry.pH - IS10500_PH_MAX) / 2.0f;         // reaches 1.0 at pH 10.5
        if (s > severity) severity = s;
      }
      if (severity > 1.0f) severity = 1.0f;

      // Scale proportionally from 0.88f up to 0.99f
      float effectiveContamProb = fmaxf(mlContamProb, 0.88f + severity * 0.11f);
      if (effectiveContamProb > 0.99f) effectiveContamProb = 0.99f;

      telemetry.probContaminated = effectiveContamProb;
      float rem = 1.0f - telemetry.probContaminated;
      telemetry.probCaution = rem * 0.8f;
      telemetry.probSafe = rem * 0.2f;
    } else if (telemetry.finalStatus == CLASS_CAUTION) {
      float effectiveContamProb = fmaxf(mlContamProb, 0.45f);
      telemetry.probContaminated = effectiveContamProb;
      float rem = 1.0f - telemetry.probContaminated;
      telemetry.probCaution = rem * 0.7f;
      telemetry.probSafe = rem * 0.3f;
    }
  } else {
    telemetry.failSafeTriggered = false;
    telemetry.finalStatus = telemetry.predictedClass;
    snprintf(telemetry.alertReason, sizeof(telemetry.alertReason), "All params within IS 10500");
  }
}
