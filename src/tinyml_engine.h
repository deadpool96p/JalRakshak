#ifndef TINYML_ENGINE_H
#define TINYML_ENGINE_H

#include "sensors.h"

// ============================================================================
// TINYML MODEL DEFINITIONS
// ============================================================================
#define MODEL_INPUT_SIZE   4
#define MODEL_OUTPUT_SIZE  3

#define CLASS_SAFE          0  // 0 = SAFE (Normal Potable Water)
#define CLASS_CAUTION       1  // 1 = CAUTION (Borderline Quality / Sensor Drift)
#define CLASS_CONTAMINATED  2  // 2 = CONTAMINATED (Severe Health Hazard)

void tinyml_init();
void tinyml_infer(WaterTelemetry &telemetry);

#endif // TINYML_ENGINE_H
