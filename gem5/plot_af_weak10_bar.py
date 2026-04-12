#!/usr/bin/env python3
"""
Grouped bar charts of mean AF (weakest 10%) averaged over all injection rates,
one group per traffic type with bars for LARE and Clotho-GAR.
  - plots/barchart/af_weak10_bar_links.png   (EM)
  - plots/barchart/af_weak10_bar_routers.png (HCI)

AF = mean MTTF_algo (weakest 10%) / mean MTTF_DOR (weakest 10% DOR)
"""

import os
import re
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RESULTS = "m5out/results"
OUTDIR  = "plots/barchart"
os.makedirs(OUTDIR, exist_ok=True)

TRAFFICS = [
    "uniform",
    "shuffle",
    "transpose",
    "tornado",
    "bit_reverse",
    "bit_rotation",
    "neighbor",
]

# Clotho sweep uses the gem5 synthetic name as the directory component.
# "uniform" in TRAFFICS corresponds to "uniform_random" in Clotho dirs.
CLOTHO_NAME = {
    "uniform":    "uniform_random",
    "shuffle":    "shuffle",
    "transpose":  "transpose",
    "tornado":    "tornado",
    "bit_reverse":"bit_reverse",
    "bit_rotation":"bit_rotation",
    "neighbor":   "neighbor",
}


def mttf_paths(traffic, rate):
    rate_str = f"{float(rate):.2f}"
    dor_dir = os.path.join(RESULTS, f"dor_{traffic}_rand_vnet{rate_str}")
    rl_dir  = os.path.join(RESULTS, f"rl_{traffic}_rand_vnet{rate_str}")
    clotho_name = CLOTHO_NAME[traffic]
    clotho_dir  = os.path.join(RESULTS, f"clotho_{clotho_name}_rand_vnet{rate_str}")

    if traffic == "uniform":
        dor_file    = os.path.join(dor_dir,    f"mttf_dor_rand_vnet{rate_str}.txt")
        rl_file     = os.path.join(rl_dir,     f"mttf_rl_rand_vnet{rate_str}.txt")
    else:
        dor_file    = os.path.join(dor_dir,    f"mttf_dor_{traffic}_rand_vnet{rate_str}.txt")
        rl_file     = os.path.join(rl_dir,     f"mttf_rl_{traffic}_rand_vnet{rate_str}.txt")

    clotho_file = os.path.join(clotho_dir, f"mttf_clotho_{clotho_name}_rand_vnet{rate_str}.txt")

    return dor_file, rl_file, clotho_file


def parse_mttf(path):
    routers, links = {}, {}
    section = None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("#"):
                if "router_id" in line:
                    section = "routers"
                elif "rl_link_idx" in line:
                    section = "links"
                continue
            if not line:
                continue
            parts = line.split()
            if section == "routers":
                routers[int(parts[1])] = float(parts[2])
            elif section == "links":
                links[int(parts[1])] = (int(parts[2]), int(parts[3]), float(parts[4]))
    return routers, links


def compute_af(dor_r, dor_l, algo_r, algo_l):
    """AF of algo vs DOR on weakest 10% of components."""
    floor = sum(1 for lid in algo_l if algo_l[lid][2] > 1e20)
    if floor == len(algo_l):
        return None, None
    top_l = max(1, round(0.10 * len(dor_l)))
    top_r = max(1, round(0.10 * len(dor_r)))
    dor_weak_l  = sorted(dor_l,   key=lambda k: dor_l[k][2])[:top_l]
    dor_weak_r  = sorted(dor_r,   key=dor_r.get)[:top_r]
    algo_weak_l = sorted(algo_l,  key=lambda k: algo_l[k][2])[:top_l]
    algo_weak_r = sorted(algo_r,  key=algo_r.get)[:top_r]
    mean_algo_l  = sum(algo_l[l][2] for l in algo_weak_l) / top_l
    mean_dor_l   = sum(dor_l[l][2]  for l in dor_weak_l)  / top_l
    mean_algo_r  = sum(algo_r[r]    for r in algo_weak_r)  / top_r
    mean_dor_r   = sum(dor_r[r]     for r in dor_weak_r)   / top_r
    return mean_algo_l / mean_dor_l, mean_algo_r / mean_dor_r


def get_rates(traffic):
    pattern = re.compile(rf"^rl_{traffic}_rand_vnet(\d+\.\d+)$")
    rates = []
    for d in sorted(os.listdir(RESULTS)):
        m = pattern.match(d)
        if m:
            rates.append(float(m.group(1)))
    return sorted(rates)


# ── Collect per-traffic mean AFs ────────────────────────────────────────────

bar_labels  = []
rl_af_l,     rl_af_r     = [], []
clotho_af_l, clotho_af_r = [], []

for traffic in TRAFFICS:
    rates = get_rates(traffic)
    if not rates:
        continue

    rl_afs_l, rl_afs_r         = [], []
    clotho_afs_l, clotho_afs_r = [], []

    for rate in rates[1:]:   # skip first injection rate (near-zero load)
        dor_file, rl_file, clotho_file = mttf_paths(traffic, rate)

        if not os.path.exists(dor_file):
            continue

        try:
            dor_r, dor_l = parse_mttf(dor_file)
        except Exception as e:
            print(f"  Warning: {traffic} rate={rate} DOR: {e}")
            continue

        # LARE
        if os.path.exists(rl_file):
            try:
                rl_r, rl_l = parse_mttf(rl_file)
                af_l, af_r = compute_af(dor_r, dor_l, rl_r, rl_l)
                if af_l is not None:
                    rl_afs_l.append(af_l)
                    rl_afs_r.append(af_r)
            except Exception as e:
                print(f"  Warning: {traffic} rate={rate} RL: {e}")

        # Clotho
        if os.path.exists(clotho_file):
            try:
                cl_r, cl_l = parse_mttf(clotho_file)
                af_l, af_r = compute_af(dor_r, dor_l, cl_r, cl_l)
                if af_l is not None:
                    clotho_afs_l.append(af_l)
                    clotho_afs_r.append(af_r)
            except Exception as e:
                print(f"  Warning: {traffic} rate={rate} Clotho: {e}")

    has_rl     = len(rl_afs_l)     > 0
    has_clotho = len(clotho_afs_l) > 0

    if not has_rl and not has_clotho:
        print(f"  Skipping {traffic}: no valid data points")
        continue

    label = traffic.replace("_", "\n").title()
    bar_labels.append(label)

    rl_mean_l     = sum(rl_afs_l)     / len(rl_afs_l)     if has_rl     else float("nan")
    rl_mean_r     = sum(rl_afs_r)     / len(rl_afs_r)     if has_rl     else float("nan")
    clotho_mean_l = sum(clotho_afs_l) / len(clotho_afs_l) if has_clotho else float("nan")
    clotho_mean_r = sum(clotho_afs_r) / len(clotho_afs_r) if has_clotho else float("nan")

    rl_af_l.append(rl_mean_l);         rl_af_r.append(rl_mean_r)
    clotho_af_l.append(clotho_mean_l); clotho_af_r.append(clotho_mean_r)

    print(f"  {traffic}:"
          f"  LARE EM={rl_mean_l:.3f} HCI={rl_mean_r:.3f} ({len(rl_afs_l)} rates)"
          f"  |  Clotho EM={clotho_mean_l:.3f} HCI={clotho_mean_r:.3f} ({len(clotho_afs_l)} rates)")


# ── Plot ─────────────────────────────────────────────────────────────────────

n = len(bar_labels)
x = np.arange(n)
width = 0.35

for algo_vals, clotho_vals, ylabel, title, fname in [
    (rl_af_l, clotho_af_l,
     "Mean AF (MTTF$_{algo}$ / MTTF$_{DOR}$, EM)",
     "Link EM Wear — Mean AF by Traffic Type (weakest 10%)",
     "af_weak10_bar_links_new.png"),
    (rl_af_r, clotho_af_r,
     "Mean AF (MTTF$_{algo}$ / MTTF$_{DOR}$, HCI)",
     "Router HCI Wear — Mean AF by Traffic Type (weakest 10%)",
     "af_weak10_bar_routers_new.png"),
]:
    fig, ax = plt.subplots(figsize=(11, 5))

    bars_rl     = ax.bar(x - width/2, algo_vals,   width, label="Aging-Aware RL",       color="#3a7ebf", edgecolor="white", linewidth=0.5)
    bars_clotho = ax.bar(x + width/2, clotho_vals, width, label="Clotho-GAR", color="#e07b39", edgecolor="white", linewidth=0.5)

    ax.axhline(1.0, color="black", linestyle="--", linewidth=1, label="DOR baseline (AF=1)")

    for bar, val in list(zip(bars_rl, algo_vals)) + list(zip(bars_clotho, clotho_vals)):
        if not np.isnan(val):
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.02,
                    f"{val:.2f}", ha="center", va="bottom", fontsize=8)

    ax.set_xticks(x)
    ax.set_xticklabels(bar_labels, fontsize=10)
    ax.set_ylabel(ylabel, fontsize=11)
    ax.set_title(title, fontsize=12)
    ax.legend(fontsize=10)
    ax.grid(True, axis="y", alpha=0.3)
    ax.set_ylim(bottom=0)

    fig.tight_layout()
    outpath = os.path.join(OUTDIR, fname)
    fig.savefig(outpath, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {outpath}")

print("\nDone.")
