#!/usr/bin/env python3
"""
===============================================================================
JALRAKSHAK: Multi-Source Water Quality Dataset Preparation Pipeline
===============================================================================
Generates / curates >= 5,000 labeled real-world aligned water quality samples:
  - Inputs: [ph, tds_ppm, turbidity_ntu, temp_c]
  - Labels: 0 = SAFE, 1 = CAUTION, 2 = CONTAMINATED
Standards compliance:
  - Bureau of Indian Standards (BIS) IS 10500:2012
  - Central Pollution Control Board (CPCB) India Groundwater & Surface Profiles
  - World Health Organization (WHO) Potability Guidelines
===============================================================================
"""

import os
import sys
import numpy as np

def generate_jalrakshak_dataset(n_samples=6000, output_csv="data/jalrakshak_water_dataset.csv", seed=42):
    np.random.seed(seed)
    print("=" * 70)
    print("   JALRAKSHAK: Preparing Real-World Aligned Water Dataset")
    print("   Standard: BIS IS 10500:2012 & CPCB India Hydrogeological Benchmarks")
    print("=" * 70)

    # Class breakdown:
    # 0 = SAFE (~35%), 1 = CAUTION (~30%), 2 = CONTAMINATED (~35%)
    n_safe = int(n_samples * 0.35)
    n_caution = int(n_samples * 0.30)
    n_contam = n_samples - n_safe - n_caution

    # -------------------------------------------------------------------------
    # 1. CLASS 0: SAFE (Within IS 10500 Acceptable Limits)
    # Treated Municipal Tap, RO Purified, Clean Mountain/River Spring
    # - pH: 6.80 to 8.20 (Acceptable: 6.5 - 8.5)
    # - TDS: 45 to 300 ppm (Acceptable: < 500 ppm)
    # - Turbidity: 0.10 to 1.80 NTU (Acceptable: < 1.0 - 2.0 NTU)
    # - Temperature: 18.0 to 28.0 °C
    # -------------------------------------------------------------------------
    ph_safe = np.random.normal(loc=7.25, scale=0.35, size=n_safe).clip(6.60, 8.40)
    tds_safe = np.random.exponential(scale=70.0, size=n_safe) + 50.0
    tds_safe = tds_safe.clip(45.0, 320.0)
    turb_safe = np.random.exponential(scale=0.45, size=n_safe).clip(0.05, 1.80)
    temp_safe = np.random.normal(loc=24.5, scale=2.8, size=n_safe).clip(16.0, 30.0)
    labels_safe = np.zeros(n_safe, dtype=int)

    # -------------------------------------------------------------------------
    # 2. CLASS 1: CAUTION / DRIFT (Borderline, High Mineral Borewell, Monsoon Runoff)
    # - High TDS borewell water (350 - 500 ppm)
    # - Slight post-filtration turbidity haze (2.0 - 5.0 NTU)
    # - Minor pH shift: 6.2 - 6.5 (mild organic acid) or 8.5 - 9.0 (limestone aquifer)
    # - Temperature: 20.0 to 36.0 °C
    # -------------------------------------------------------------------------
    n_c_ph = n_caution // 3
    n_c_tds = n_caution // 3
    n_c_turb = n_caution - (2 * n_c_ph)

    # Sub-case A: pH drift
    ph_drift_lo = np.random.uniform(6.15, 6.49, size=n_c_ph // 2)
    ph_drift_hi = np.random.uniform(8.51, 9.00, size=n_c_ph - (n_c_ph // 2))
    ph_c1 = np.concatenate([ph_drift_lo, ph_drift_hi])
    tds_c1 = np.random.uniform(150.0, 420.0, size=n_c_ph)
    turb_c1 = np.random.uniform(0.5, 3.5, size=n_c_ph)

    # Sub-case B: Hard water / elevated mineral borewell (near 500 ppm limit)
    ph_c2 = np.random.normal(loc=7.6, scale=0.3, size=n_c_tds).clip(6.7, 8.3)
    tds_c2 = np.random.uniform(340.0, 500.0, size=n_c_tds)
    turb_c2 = np.random.uniform(0.8, 3.8, size=n_c_tds)

    # Sub-case C: Turbidity haze (construction/monsoon dust near 5 NTU)
    ph_c3 = np.random.normal(loc=7.1, scale=0.4, size=n_c_turb).clip(6.6, 8.2)
    tds_c3 = np.random.uniform(180.0, 450.0, size=n_c_turb)
    turb_c3 = np.random.uniform(2.1, 5.0, size=n_c_turb)

    ph_caution = np.concatenate([ph_c1, ph_c2, ph_c3])
    tds_caution = np.concatenate([tds_c1, tds_c2, tds_c3])
    turb_caution = np.concatenate([turb_c1, turb_c2, turb_c3])
    temp_caution = np.random.normal(loc=26.0, scale=3.5, size=n_caution).clip(18.0, 36.0)
    labels_caution = np.ones(n_caution, dtype=int)

    # -------------------------------------------------------------------------
    # 3. CLASS 2: CONTAMINATED (Severe IS 10500 Breach / Toxic / Sewage Influx)
    # - Acidic industrial/battery discharge: pH < 6.0 (down to 2.5)
    # - Alkaline caustic/detergent spill: pH > 9.2 (up to 12.5)
    # - Saline intrusion / heavy dissolved solids: TDS 520 - 2500 ppm
    # - Heavy monsoon mud / untreated drainage: Turbidity 5.2 - 250 NTU
    # -------------------------------------------------------------------------
    n_k_acid = n_contam // 4
    n_k_alkali = n_contam // 4
    n_k_tds = n_contam // 4
    n_k_turb = n_contam - (3 * n_k_acid)

    # Acid spike
    ph_k1 = np.random.uniform(2.5, 5.95, size=n_k_acid)
    tds_k1 = np.random.uniform(150.0, 950.0, size=n_k_acid)
    turb_k1 = np.random.uniform(0.5, 35.0, size=n_k_acid)

    # Alkaline spike
    ph_k2 = np.random.uniform(9.15, 12.8, size=n_k_alkali)
    tds_k2 = np.random.uniform(200.0, 1100.0, size=n_k_alkali)
    turb_k2 = np.random.uniform(0.8, 40.0, size=n_k_alkali)

    # Hyper-saline / sewage TDS breach
    ph_k3 = np.random.uniform(6.2, 8.8, size=n_k_tds)
    tds_k3 = np.random.uniform(520.0, 2200.0, size=n_k_tds)
    turb_k3 = np.random.uniform(2.0, 60.0, size=n_k_tds)

    # Heavy mud / biological silt breach
    ph_k4 = np.random.uniform(6.3, 8.6, size=n_k_turb)
    tds_k4 = np.random.uniform(220.0, 950.0, size=n_k_turb)
    turb_k4 = np.random.uniform(5.5, 180.0, size=n_k_turb)

    ph_contam = np.concatenate([ph_k1, ph_k2, ph_k3, ph_k4])
    tds_contam = np.concatenate([tds_k1, tds_k2, tds_k3, tds_k4])
    turb_contam = np.concatenate([turb_k1, turb_k2, turb_k3, turb_k4])
    temp_contam = np.random.normal(loc=27.5, scale=5.0, size=n_contam).clip(10.0, 48.0)
    labels_contam = np.full(n_contam, 2, dtype=int)

    # -------------------------------------------------------------------------
    # Stack & Add Realistic ESP32 ADC Gaussian Noise
    # -------------------------------------------------------------------------
    ph = np.concatenate([ph_safe, ph_caution, ph_contam])
    tds = np.concatenate([tds_safe, tds_caution, tds_contam])
    turb = np.concatenate([turb_safe, turb_caution, turb_contam])
    temp = np.concatenate([temp_safe, temp_caution, temp_contam])
    labels = np.concatenate([labels_safe, labels_caution, labels_contam])

    # Sensor noise augmentation (±0.03 pH, ±2.5 ppm TDS, ±0.15 NTU, ±0.2 °C)
    ph += np.random.normal(0, 0.03, size=len(ph))
    tds += np.random.normal(0, 2.5, size=len(tds))
    turb += np.random.normal(0, 0.15, size=len(turb))
    temp += np.random.normal(0, 0.20, size=len(temp))

    ph = ph.clip(0.0, 14.0)
    tds = tds.clip(0.0, 3000.0)
    turb = turb.clip(0.0, 3000.0)
    temp = temp.clip(0.0, 60.0)

    # Shuffle rows deterministically
    indices = np.random.permutation(len(labels))
    ph, tds, turb, temp, labels = ph[indices], tds[indices], turb[indices], temp[indices], labels[indices]

    # Save to CSV
    os.makedirs(os.path.dirname(output_csv), exist_ok=True)
    with open(output_csv, "w", encoding="utf-8") as f:
        f.write("ph,tds_ppm,turbidity_ntu,temp_c,label\n")
        for i in range(len(labels)):
            f.write(f"{ph[i]:.2f},{tds[i]:.1f},{turb[i]:.2f},{temp[i]:.2f},{labels[i]}\n")

    print(f"\n[+] Successfully generated {len(labels)} water quality samples.")
    print(f"    File saved to: {output_csv}")
    print("\n[+] Class Distribution Statistics:")
    print(f"    - Class 0 (SAFE):         {np.sum(labels == 0)} samples ({np.mean(labels == 0)*100:.1f}%)")
    print(f"    - Class 1 (CAUTION):      {np.sum(labels == 1)} samples ({np.mean(labels == 1)*100:.1f}%)")
    print(f"    - Class 2 (CONTAMINATED): {np.sum(labels == 2)} samples ({np.mean(labels == 2)*100:.1f}%)")
    print("\n[+] Metric Summaries (Mean ± Std):")
    print(f"    - pH:        {np.mean(ph):.2f} ± {np.std(ph):.2f}  [Min: {np.min(ph):.2f}, Max: {np.max(ph):.2f}]")
    print(f"    - TDS (ppm): {np.mean(tds):.1f} ± {np.std(tds):.1f}  [Min: {np.min(tds):.1f}, Max: {np.max(tds):.1f}]")
    print(f"    - Turb (NTU):{np.mean(turb):.2f} ± {np.std(turb):.2f}  [Min: {np.min(turb):.2f}, Max: {np.max(turb):.2f}]")
    print(f"    - Temp (°C): {np.mean(temp):.1f} ± {np.std(temp):.1f}  [Min: {np.min(temp):.1f}, Max: {np.max(temp):.1f}]")
    print("=" * 70)

if __name__ == "__main__":
    generate_jalrakshak_dataset()
