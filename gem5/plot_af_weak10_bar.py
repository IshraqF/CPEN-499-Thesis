#!/usr/bin/env python3
"""
Bar charts of mean AF (weakest 10%) averaged over all injection rates,
one bar per traffic type.
  - plots/af_weak10_bar_links.png   (EM)
  - plots/af_weak10_bar_routers.png (HCI)

AF = mean MTTF_RL (weakest 10% RL) / mean MTTF_DOR (weakest 10% DOR)
"""

import os
import re
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

COLORS = [
    "#e07b39", "#3a7ebf", "#2ecc71", "#e74c3c",
    "#9b59b6", "#f39c12", "#1abc9c",
]


def mttf_paths(traffic, rate):
    rate_str = f"{float(rate):.2f}"
    dor_dir = os.path.join(RESULTS, f"dor_{traffic}_rand_vnet{rate_str}")
    rl_dir  = os.path.join(RESULTS, f"rl_{traffic}_rand_vnet{rate_str}")
    if traffic == "uniform":
        dor_file = os.path.join(dor_dir, f"mttf_dor_rand_vnet{rate_str}.txt")
        rl_file  = os.path.join(rl_dir,  f"mttf_rl_rand_vnet{rate_str}.txt")
    else:
        dor_file = os.path.join(dor_dir, f"mttf_dor_{traffic}_rand_vnet{rate_str}.txt")
        rl_file  = os.path.join(rl_dir,  f"mttf_rl_{traffic}_rand_vnet{rate_str}.txt")
    return dor_file, rl_file


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


def compute_af(dor_r, dor_l, rl_r, rl_l):
    floor = sum(1 for lid in rl_l if rl_l[lid][2] > 1e20)
    if floor == len(rl_l):
        return None, None
    top_l = max(1, round(0.10 * len(dor_l)))
    top_r = max(1, round(0.10 * len(dor_r)))
    dor_weak_l = sorted(dor_l, key=lambda k: dor_l[k][2])[:top_l]
    dor_weak_r = sorted(dor_r, key=dor_r.get)[:top_r]
    rl_weak_l  = sorted(rl_l,  key=lambda k: rl_l[k][2])[:top_l]
    rl_weak_r  = sorted(rl_r,  key=rl_r.get)[:top_r]
    mean_rl_l  = sum(rl_l[l][2]  for l in rl_weak_l)  / top_l
    mean_dor_l = sum(dor_l[l][2] for l in dor_weak_l) / top_l
    mean_rl_r  = sum(rl_r[r]     for r in rl_weak_r)  / top_r
    mean_dor_r = sum(dor_r[r]    for r in dor_weak_r) / top_r
    return mean_rl_l / mean_dor_l, mean_rl_r / mean_dor_r


def get_rates(traffic):
    pattern = re.compile(rf"^rl_{traffic}_rand_vnet(\d+\.\d+)$")
    rates = []
    for d in sorted(os.listdir(RESULTS)):
        m = pattern.match(d)
        if m:
            rates.append(float(m.group(1)))
    return sorted(rates)


bar_labels, bar_af_l, bar_af_r = [], [], []

for traffic in TRAFFICS:
    rates = get_rates(traffic)
    if not rates:
        continue

    afs_l, afs_r = [], []

    for rate in rates[1:]:   # skip first injection rate
        dor_file, rl_file = mttf_paths(traffic, rate)
        if not os.path.exists(dor_file) or not os.path.exists(rl_file):
            continue
        try:
            dor_r, dor_l = parse_mttf(dor_file)
            rl_r,  rl_l  = parse_mttf(rl_file)
        except Exception as e:
            print(f"  Warning: {traffic} rate={rate}: {e}")
            continue

        af_l, af_r = compute_af(dor_r, dor_l, rl_r, rl_l)
        if af_l is None:
            continue

        afs_l.append(af_l)
        afs_r.append(af_r)

    if not afs_l:
        print(f"  Skipping {traffic}: no valid data points")
        continue

    mean_l = sum(afs_l) / len(afs_l)
    mean_r = sum(afs_r) / len(afs_r)
    bar_labels.append(traffic.replace("_", "\n").title())
    bar_af_l.append(mean_l)
    bar_af_r.append(mean_r)
    print(f"  {traffic}: EM AF={mean_l:.3f}  HCI AF={mean_r:.3f}  ({len(afs_l)} rates)")

x = range(len(bar_labels))

for af_vals, ylabel, title, fname in [
    (bar_af_l,
     "Mean AF (EM MTTF$_{RL}$ / MTTF$_{DOR}$)",
     "Link EM Wear — Mean AF by Traffic Type (weakest 10%)",
     "af_weak10_bar_links.png"),
    (bar_af_r,
     "Mean AF (HCI MTTF$_{RL}$ / MTTF$_{DOR}$)",
     "Router HCI Wear — Mean AF by Traffic Type (weakest 10%)",
     "af_weak10_bar_routers.png"),
]:
    fig, ax = plt.subplots(figsize=(10, 5))
    bars = ax.bar(x, af_vals, color=COLORS[:len(bar_labels)], edgecolor="white", linewidth=0.5)
    ax.axhline(1.0, color="black", linestyle="--", linewidth=1, label="DOR baseline (AF=1)")

    # Value labels on top of each bar
    for bar, val in zip(bars, af_vals):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.02,
                f"{val:.2f}", ha="center", va="bottom", fontsize=9)

    ax.set_xticks(list(x))
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
