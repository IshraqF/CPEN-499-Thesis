#!/usr/bin/env python3
"""
Plot average packet latency (cycles) for DOR, RL, and Clotho-GAR
over injection rates, one plot per traffic type.

Output: plots/latency/latency_<traffic>.png
"""

import os
import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RESULTS = "m5out/results"
OUTDIR  = "plots/latency"
os.makedirs(OUTDIR, exist_ok=True)

TRAFFICS = [
    "uniform",
    "shuffle",
    "transpose",
    "tornado",
    "bit_complement",
    "bit_reverse",
    "bit_rotation",
    "neighbor",
]

# Clotho sweep uses gem5 synthetic name as the directory component.
CLOTHO_NAME = {
    "uniform":        "uniform_random",
    "shuffle":        "shuffle",
    "transpose":      "transpose",
    "tornado":        "tornado",
    "bit_complement": "bit_complement",
    "bit_reverse":    "bit_reverse",
    "bit_rotation":   "bit_rotation",
    "neighbor":       "neighbor",
}


def csv_paths(traffic):
    dor_csv    = os.path.join(RESULTS, f"dor_{traffic}_rand_vnet_summary.csv")
    rl_csv     = os.path.join(RESULTS, f"rl_{traffic}_rand_vnet_summary.csv")
    if not os.path.exists(rl_csv):
        rl_csv = os.path.join(RESULTS, f"rl_bit_{traffic}_rand_vnet_summary.csv")
    clotho_csv = os.path.join(RESULTS, f"clotho_{CLOTHO_NAME[traffic]}_rand_vnet_summary.csv")
    return dor_csv, rl_csv, clotho_csv


def read_latency(csv_path):
    rates, latencies = [], []
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                rates.append(float(row["injection_rate"]))
                latencies.append(float(row["avg_packet_latency_cycles"]))
            except (KeyError, ValueError):
                continue
    return rates, latencies


for traffic in TRAFFICS:
    dor_csv, rl_csv, clotho_csv = csv_paths(traffic)

    if not os.path.exists(dor_csv):
        print(f"  Skipping {traffic}: DOR CSV not found ({dor_csv})")
        continue

    dor_rates, dor_lat = read_latency(dor_csv)
    if not dor_rates:
        print(f"  Skipping {traffic}: empty DOR CSV")
        continue

    rl_data     = read_latency(rl_csv)     if os.path.exists(rl_csv)     else ([], [])
    clotho_data = read_latency(clotho_csv) if os.path.exists(clotho_csv) else ([], [])

    fig, ax = plt.subplots(figsize=(7, 4.5))

    ax.plot(dor_rates,       dor_lat,        "o-",  color="#3a7ebf", linewidth=2,
            markersize=5, label="DOR")

    if rl_data[0]:
        ax.plot(rl_data[0],     rl_data[1],     "s--", color="#e07b39", linewidth=2,
                markersize=5, label="Aging-Aware RL")

    if clotho_data[0]:
        ax.plot(clotho_data[0], clotho_data[1], "^-.", color="#2ecc71", linewidth=2,
                markersize=5, label="Clotho-GAR")

    ax.set_xlabel("Injection Rate (Packet/Cycle/Node)", fontsize=11)
    ax.set_ylabel("Average Packet Latency (Cycles)", fontsize=11)
    ax.set_title(
        f"Average Packet Latency — {traffic.replace('_', ' ').title()} Traffic",
        fontsize=12,
    )
    ax.legend(fontsize=10)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=0)
    ax.set_ylim(0, 100)

    plt.tight_layout()
    outpath = os.path.join(OUTDIR, f"latency_{traffic}.png")
    plt.savefig(outpath, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  Saved: {outpath}")

print("\nDone.")
