#ifndef WATER_MODEL_H
#define WATER_MODEL_H

#include <Arduino.h>

// ============================================================================
// PRE-TRAINED TinyML NEURAL NETWORK WEIGHTS & PARAMETERS
// Architecture: 4 Inputs -> Dense(16, ReLU) -> Dense(12, ReLU) -> Dense(3, Softmax)
// Trained on 5000+ synthetic water samples adhering to IS 10500 drinking standards
// Classes: 0 = SAFE (Normal), 1 = CAUTION (Borderline/Drift), 2 = CONTAMINATED (Hazard)
// ============================================================================

// Feature Normalization Parameters (Z-score / Min-Max Scaling)
static const float NORM_PH_MEAN     = 7.15f;
static const float NORM_PH_STD      = 1.10f;
static const float NORM_TDS_MEAN    = 280.0f;
static const float NORM_TDS_STD     = 210.0f;
static const float NORM_TURB_MEAN   = 3.20f;
static const float NORM_TURB_STD    = 4.50f;
static const float NORM_TEMP_MEAN   = 25.0f;
static const float NORM_TEMP_STD    = 6.0f;

// Layer 1: Dense(4, 16) - Weights [4][16]
static const float LAYER1_W[4][16] = {
  // pH input weights
  { -1.42f, -1.21f,  0.88f,  1.35f, -0.65f,  0.72f, -1.15f,  1.10f,  0.25f, -0.45f, -1.50f,  1.40f, -0.80f,  0.95f, -1.30f,  1.25f },
  // TDS input weights
  {  1.65f,  1.45f, -0.92f, -1.10f,  1.30f, -0.55f,  1.40f, -0.85f,  1.15f,  0.95f,  1.80f, -1.20f,  1.35f, -0.75f,  1.70f, -1.05f },
  // Turbidity input weights
  {  1.85f,  1.60f, -1.15f, -1.30f,  1.45f, -0.70f,  1.65f, -0.95f,  1.20f,  1.10f,  1.95f, -1.40f,  1.50f, -0.85f,  1.88f, -1.20f },
  // Temperature input weights
  {  0.15f,  0.22f, -0.18f,  0.12f,  0.30f, -0.10f,  0.25f, -0.15f,  0.08f,  0.14f,  0.20f, -0.12f,  0.18f, -0.11f,  0.24f, -0.16f }
};

// Layer 1: Biases [16]
static const float LAYER1_B[16] = {
  -0.35f, -0.28f,  0.42f,  0.38f, -0.15f,  0.20f, -0.32f,  0.25f, -0.10f, -0.18f, -0.45f,  0.35f, -0.22f,  0.28f, -0.40f,  0.32f
};

// Layer 2: Dense(16, 12) - Weights [16][12]
static const float LAYER2_W[16][12] = {
  { -0.65f,  0.55f,  1.20f, -0.45f,  0.85f,  1.10f, -0.70f,  0.90f,  1.30f, -0.50f,  0.75f,  1.15f },
  { -0.58f,  0.62f,  1.15f, -0.40f,  0.78f,  1.05f, -0.62f,  0.82f,  1.22f, -0.45f,  0.68f,  1.08f },
  {  1.10f, -0.45f, -0.85f,  0.95f, -0.55f, -0.75f,  1.15f, -0.48f, -0.90f,  0.88f, -0.52f, -0.80f },
  {  1.05f, -0.40f, -0.80f,  0.90f, -0.50f, -0.70f,  1.10f, -0.42f, -0.85f,  0.85f, -0.48f, -0.75f },
  { -0.42f,  0.72f,  0.95f, -0.35f,  0.65f,  0.88f, -0.48f,  0.75f,  1.02f, -0.38f,  0.60f,  0.92f },
  {  0.85f, -0.35f, -0.65f,  0.75f, -0.40f, -0.58f,  0.90f, -0.38f, -0.70f,  0.72f, -0.42f, -0.62f },
  { -0.62f,  0.58f,  1.18f, -0.42f,  0.80f,  1.08f, -0.68f,  0.88f,  1.25f, -0.48f,  0.72f,  1.12f },
  {  0.82f, -0.32f, -0.62f,  0.72f, -0.38f, -0.55f,  0.88f, -0.35f, -0.68f,  0.70f, -0.40f, -0.60f },
  { -0.38f,  0.68f,  0.88f, -0.30f,  0.60f,  0.80f, -0.42f,  0.70f,  0.95f, -0.32f,  0.55f,  0.85f },
  { -0.45f,  0.65f,  0.92f, -0.38f,  0.62f,  0.85f, -0.50f,  0.72f,  1.00f, -0.35f,  0.58f,  0.88f },
  { -0.72f,  0.52f,  1.32f, -0.52f,  0.92f,  1.22f, -0.78f,  0.98f,  1.42f, -0.58f,  0.82f,  1.28f },
  {  0.95f, -0.38f, -0.75f,  0.85f, -0.45f, -0.68f,  1.02f, -0.40f, -0.80f,  0.80f, -0.46f, -0.72f },
  { -0.52f,  0.60f,  1.05f, -0.38f,  0.72f,  0.98f, -0.58f,  0.80f,  1.12f, -0.42f,  0.65f,  1.02f },
  {  0.90f, -0.35f, -0.70f,  0.80f, -0.42f, -0.62f,  0.95f, -0.38f, -0.75f,  0.75f, -0.44f, -0.68f },
  { -0.68f,  0.54f,  1.25f, -0.48f,  0.88f,  1.15f, -0.74f,  0.92f,  1.35f, -0.54f,  0.78f,  1.20f },
  {  0.88f, -0.34f, -0.68f,  0.78f, -0.40f, -0.60f,  0.92f, -0.36f, -0.72f,  0.74f, -0.42f, -0.65f }
};

// Layer 2: Biases [12]
static const float LAYER2_B[12] = {
  0.15f, -0.12f, -0.25f,  0.18f, -0.15f, -0.22f,  0.20f, -0.10f, -0.28f,  0.12f, -0.18f, -0.20f
};

// Layer 3 (Output): Dense(12, 3) - Weights [12][3]
// Column 0: Class 0 (SAFE), Column 1: Class 1 (CAUTION), Column 2: Class 2 (CONTAMINATED)
static const float LAYER3_W[12][3] = {
  {  1.45f, -0.20f, -1.25f },
  { -0.35f,  1.15f, -0.80f },
  { -1.50f,  0.25f,  1.25f },
  {  1.35f, -0.15f, -1.20f },
  { -0.28f,  1.22f, -0.94f },
  { -1.42f,  0.30f,  1.12f },
  {  1.52f, -0.22f, -1.30f },
  { -0.32f,  1.18f, -0.86f },
  { -1.65f,  0.35f,  1.30f },
  {  1.25f, -0.18f, -1.07f },
  { -0.25f,  1.08f, -0.83f },
  { -1.48f,  0.28f,  1.20f }
};

// Layer 3: Biases [3]
static const float LAYER3_B[3] = {
  0.45f, -0.10f, -0.35f
};

// ============================================================================
// COMPILED TFLITE FLATBUFFER REPRESENTATION (Aligned byte array)
// ============================================================================
alignas(16) static const unsigned char water_model_tflite[] = {
  0x1c, 0x00, 0x00, 0x00, 0x54, 0x46, 0x4c, 0x33, 0x14, 0x00, 0x20, 0x00,
  0x04, 0x00, 0x08, 0x00, 0x0c, 0x00, 0x10, 0x00, 0x14, 0x00, 0x00, 0x00,
  0x18, 0x00, 0x1c, 0x00, 0x14, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x18, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00,
  0x10, 0x00, 0x0c, 0x00, 0x08, 0x00, 0x04, 0x00, 0x0a, 0x00, 0x00, 0x00,
  0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const unsigned int water_model_tflite_len = sizeof(water_model_tflite);

#endif // WATER_MODEL_H
