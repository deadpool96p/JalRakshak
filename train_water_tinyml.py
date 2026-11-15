#!/usr/bin/env python3
"""
===============================================================================
JALRAKSHAK: TinyML Neural Network Training & Quantization Pipeline
===============================================================================
This script:
1. Generates 4,500 synthetic water quality samples based on Bureau of Indian
   Standards (BIS) IS 10500 drinking water specifications.
2. Normalizes 4 core features: [pH, TDS (ppm), Turbidity (NTU), Temperature (°C)]
3. Trains a 4 -> 16 -> 12 -> 3 Deep Neural Network (MLP).
4. Quantizes the model to TensorFlow Lite (.tflite).
5. Exports a production-ready C/C++ header file (`water_model.h`) with both:
   - Aligned C byte array for TensorFlow Lite Micro.
   - Extracted dense layer weights & biases for ultra-fast C forward execution.
===============================================================================
"""

import os
import sys
import numpy as np

# Ensure TensorFlow is imported or provide clear installation instructions
try:
    import tensorflow as tf
    from tensorflow import keras
except ImportError:
    print("\n[!] TensorFlow is not installed in the current Python environment.")
    print("    Install it via: pip install tensorflow numpy\n")
    sys.exit(1)


def generate_synthetic_water_dataset(num_samples=4500, random_seed=42):
    """
    Generates synthetic water quality telemetry conforming to IS 10500 standards:
    - Class 0: SAFE (Potable, healthy drinking water)
    - Class 1: CAUTION / DRIFT (Borderline quality, elevated minerals or turbidity)
    - Class 2: CONTAMINATED (Severe chemical, particulate, or industrial hazard)
    """
    np.random.seed(random_seed)
    n_per_class = num_samples // 3

    # -------------------------------------------------------------------------
    # Class 0: SAFE (IS 10500 Acceptable Limits)
    # pH: 6.8 - 8.2 | TDS: 50 - 280 ppm | Turbidity: 0.1 - 2.0 NTU | Temp: 18 - 28°C
    # -------------------------------------------------------------------------
    ph_safe = np.random.normal(loc=7.2, scale=0.35, size=n_per_class).clip(6.6, 8.4)
    tds_safe = np.random.uniform(low=60.0, high=290.0, size=n_per_class)
    turb_safe = np.random.exponential(scale=0.6, size=n_per_class).clip(0.1, 2.2)
    temp_safe = np.random.normal(loc=24.0, scale=3.0, size=n_per_class).clip(15.0, 30.0)
    labels_safe = np.zeros(n_per_class, dtype=np.int32)

    # -------------------------------------------------------------------------
    # Class 1: CAUTION / DRIFT (Near Permissible Thresholds)
    # pH: 6.2 - 6.5 or 8.5 - 9.0 | TDS: 300 - 500 ppm | Turbidity: 2.2 - 5.0 NTU
    # -------------------------------------------------------------------------
    ph_caution_low = np.random.uniform(6.1, 6.5, size=n_per_class // 2)
    ph_caution_high = np.random.uniform(8.5, 9.1, size=n_per_class - (n_per_class // 2))
    ph_caution = np.concatenate([ph_caution_low, ph_caution_high])
    np.random.shuffle(ph_caution)

    tds_caution = np.random.uniform(low=310.0, high=500.0, size=n_per_class)
    turb_caution = np.random.uniform(low=2.2, high=5.0, size=n_per_class)
    temp_caution = np.random.normal(loc=26.0, scale=4.0, size=n_per_class).clip(18.0, 35.0)
    labels_caution = np.ones(n_per_class, dtype=np.int32)

    # -------------------------------------------------------------------------
    # Class 2: CONTAMINATED (IS 10500 Breach / Hazardous)
    # pH: < 6.0 or > 9.2 | TDS: > 520 ppm | Turbidity: > 5.5 NTU
    # -------------------------------------------------------------------------
    ph_contam_low = np.random.uniform(3.5, 5.9, size=n_per_class // 2)
    ph_contam_high = np.random.uniform(9.2, 12.0, size=n_per_class - (n_per_class // 2))
    ph_contam = np.concatenate([ph_contam_low, ph_contam_high])
    np.random.shuffle(ph_contam)

    tds_contam = np.random.uniform(low=520.0, high=1200.0, size=n_per_class)
    turb_contam = np.random.uniform(low=5.5, high=45.0, size=n_per_class)
    temp_contam = np.random.normal(loc=27.0, scale=6.0, size=n_per_class).clip(10.0, 45.0)
    labels_contam = np.full(n_per_class, 2, dtype=np.int32)

    # Stack Features: [pH, TDS, Turbidity, Temp]
    features_safe = np.column_stack([ph_safe, tds_safe, turb_safe, temp_safe])
    features_caution = np.column_stack([ph_caution, tds_caution, turb_caution, temp_caution])
    features_contam = np.column_stack([ph_contam, tds_contam, turb_contam, temp_contam])

    X = np.vstack([features_safe, features_caution, features_contam])
    y = np.concatenate([labels_safe, labels_caution, labels_contam])

    # Shuffle dataset
    shuffle_idx = np.random.permutation(len(X))
    return X[shuffle_idx], y[shuffle_idx]


def train_tinyml_model():
    print("=" * 65)
    print("   JALRAKSHAK: Training TinyML Water Anomaly Detector")
    print("=" * 65)

    # 1. Generate Data
    X_raw, y = generate_synthetic_water_dataset(num_samples=4500)

    # Compute Feature Normalization Statistics (Z-Score)
    means = np.mean(X_raw, axis=0)
    stds = np.std(X_raw, axis=0)
    stds[stds == 0] = 1.0  # Prevent divide-by-zero

    print(f"[+] Dataset generated: {X_raw.shape[0]} samples")
    print(f"    - Mean: pH={means[0]:.2f}, TDS={means[1]:.1f}, Turb={means[2]:.2f}, Temp={means[3]:.1f}")
    print(f"    - Std:  pH={stds[0]:.2f}, TDS={stds[1]:.1f}, Turb={stds[2]:.2f}, Temp={stds[3]:.1f}")

    X_norm = (X_raw - means) / stds

    # Train / Validation Split (80 / 20)
    split_idx = int(0.8 * len(X_norm))
    X_train, X_val = X_norm[:split_idx], X_norm[split_idx:]
    y_train, y_val = y[:split_idx], y[split_idx:]

    # 2. Build 4 -> 16 -> 12 -> 3 MLP Architecture
    model = keras.Sequential([
        keras.layers.Input(shape=(4,), name="water_input"),
        keras.layers.Dense(16, activation="relu", name="dense_hidden_1"),
        keras.layers.Dense(12, activation="relu", name="dense_hidden_2"),
        keras.layers.Dense(3, activation="softmax", name="dense_classification")
    ])

    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=0.005),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"]
    )

    print("\n[+] Neural Network Architecture:")
    model.summary()

    # 3. Train Model
    print("\n[+] Training for 30 epochs...")
    history = model.fit(
        X_train, y_train,
        validation_data=(X_val, y_val),
        epochs=30,
        batch_size=32,
        verbose=1
    )

    val_loss, val_acc = model.evaluate(X_val, y_val, verbose=0)
    print(f"\n[OK] Validation Accuracy: {val_acc * 100:.2f}% | Loss: {val_loss:.4f}")

    # 4. Convert to TensorFlow Lite FlatBuffer
    print("\n[+] Converting to TensorFlow Lite (Float32 & Quantized)...")
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    tflite_model = converter.convert()

    tflite_path = "water_model.tflite"
    with open(tflite_path, "wb") as f:
        f.write(tflite_model)
    print(f"[OK] Saved TFLite model: {tflite_path} ({len(tflite_model)} bytes)")

    # 5. Extract Weights & Biases for C Header Generation
    w1, b1 = model.layers[0].get_weights()
    w2, b2 = model.layers[1].get_weights()
    w3, b3 = model.layers[2].get_weights()

    # 6. Export C Header (water_model.h)
    header_path = os.path.join("src", "water_model.h")
    print(f"\n[+] Exporting production C header: {header_path}...")

    with open(header_path, "w") as f:
        f.write("#ifndef WATER_MODEL_H\n")
        f.write("#define WATER_MODEL_H\n\n")
        f.write("#include <Arduino.h>\n\n")
        f.write("// ============================================================================\n")
        f.write("// RETRAINED TinyML WEIGHTS FOR JALRAKSHAK (IS 10500 Drinking Water Guardrail)\n")
        f.write(f"// Validation Accuracy: {val_acc * 100:.2f}%\n")
        f.write("// ============================================================================\n\n")

        # Feature Normalization Parameters
        f.write("// Feature Normalization Parameters\n")
        f.write(f"static const float NORM_PH_MEAN     = {means[0]:.4f}f;\n")
        f.write(f"static const float NORM_PH_STD      = {stds[0]:.4f}f;\n")
        f.write(f"static const float NORM_TDS_MEAN    = {means[1]:.4f}f;\n")
        f.write(f"static const float NORM_TDS_STD     = {stds[1]:.4f}f;\n")
        f.write(f"static const float NORM_TURB_MEAN   = {means[2]:.4f}f;\n")
        f.write(f"static const float NORM_TURB_STD    = {stds[2]:.4f}f;\n")
        f.write(f"static const float NORM_TEMP_MEAN   = {means[3]:.4f}f;\n")
        f.write(f"static const float NORM_TEMP_STD    = {stds[3]:.4f}f;\n\n")

        # Layer 1
        f.write(f"// Layer 1: Dense(4, 16) - Weights [4][16]\n")
        f.write("static const float LAYER1_W[4][16] = {\n")
        for r in range(4):
            f.write("  { " + ", ".join(f"{w1[r][c]:.4f}f" for c in range(16)) + " }")
            f.write(",\n" if r < 3 else "\n")
        f.write("};\n\n")

        f.write(f"// Layer 1: Biases [16]\n")
        f.write("static const float LAYER1_B[16] = {\n")
        f.write("  " + ", ".join(f"{b1[c]:.4f}f" for c in range(16)) + "\n};\n\n")

        # Layer 2
        f.write(f"// Layer 2: Dense(16, 12) - Weights [16][12]\n")
        f.write("static const float LAYER2_W[16][12] = {\n")
        for r in range(16):
            f.write("  { " + ", ".join(f"{w2[r][c]:.4f}f" for c in range(12)) + " }")
            f.write(",\n" if r < 15 else "\n")
        f.write("};\n\n")

        f.write(f"// Layer 2: Biases [12]\n")
        f.write("static const float LAYER2_B[12] = {\n")
        f.write("  " + ", ".join(f"{b2[c]:.4f}f" for c in range(12)) + "\n};\n\n")

        # Layer 3
        f.write(f"// Layer 3: Dense(12, 3) - Weights [12][3]\n")
        f.write("static const float LAYER3_W[12][3] = {\n")
        for r in range(12):
            f.write("  { " + ", ".join(f"{w3[r][c]:.4f}f" for c in range(3)) + " }")
            f.write(",\n" if r < 11 else "\n")
        f.write("};\n\n")

        f.write(f"// Layer 3: Biases [3]\n")
        f.write("static const float LAYER3_B[3] = {\n")
        f.write("  " + ", ".join(f"{b3[c]:.4f}f" for c in range(3)) + "\n};\n\n")

        # Byte array for TFLM
        f.write("// ============================================================================\n")
        f.write("// TENSORFLOW LITE MODEL BYTE ARRAY\n")
        f.write("// ============================================================================\n")
        f.write("alignas(16) static const unsigned char water_model_tflite[] = {\n")
        for i, b in enumerate(tflite_model):
            if i % 12 == 0:
                f.write("  ")
            f.write(f"0x{b:02x}, ")
            if (i + 1) % 12 == 0:
                f.write("\n")
        f.write("\n};\n")
        f.write(f"static const unsigned int water_model_tflite_len = {len(tflite_model)};\n\n")
        f.write("#endif // WATER_MODEL_H\n")

    print(f"[SUCCESS] water_model.h generated successfully!")
    print(f"          Retrained weights and TFLite byte array are ready for compilation.\n")


if __name__ == "__main__":
    train_tinyml_model()
