#!/usr/bin/env python3
"""
Plot Acceleration Factor (AF = MTTF_RL / MTTF_DOR) using two perspectives:
  - "DOR weakest 10%": identify weakest 10% of components by DOR MTTF,
    compute AF for those same components in RL.
  - "RL weakest 10%":  identify weakest 10% of components by RL MTTF,
    compute AF for those same components vs DOR.

One figure per traffic type, two subplots: EM (links) and HCI (routers).
Output: plots/af_weak10/<traffic>.png
"""

import os
import re
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RESULTS = "m5out/results"
OUTDIR  = "plots/af_weak10"
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
    """
    AF = mean(RL MTTF of RL's weakest 10%) / mean(DOR MTTF of DOR's weakest 10%)

    Numerator:   average MTTF of the weakest 10% components in RL
    Denominator: average MTTF of the weakest 10% components in DOR
    Returns (af_links, af_routers), or (None, None) if result is invalid.
    """
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


for traffic in TRAFFICS:
    rates = get_rates(traffic)
    if not rates:
        print(f"  Skipping {traffic}: no RL result folders found")
        continue

    xs = []
    af_links, af_routers = [], []

    for rate in rates[1:]:   # skip first injection rate
        dor_file, rl_file = mttf_paths(traffic, rate)
        if not os.path.exists(dor_file) or not os.path.exists(rl_file):
            continue
        try:
            dor_r, dor_l = parse_mttf(dor_file)
            rl_r,  rl_l  = parse_mttf(rl_file)
        except Exception as e:
            print(f"  Warning: could not parse {rate}: {e}")
            continue

        af_l, af_r = compute_af(dor_r, dor_l, rl_r, rl_l)
        if af_l is None:
            continue

        xs.append(rate)
        af_links.append(af_l)
        af_routers.append(af_r)

    if not xs:
        print(f"  Skipping {traffic}: no valid data points")
        continue

    fig, (ax_l, ax_r) = plt.subplots(1, 2, figsize=(12, 4.5), sharey=False)
    fig.suptitle(
        f"Acceleration Factor — {traffic.replace('_', ' ').title()} Traffic\n"
        f"AF = mean MTTF$_{{RL}}$(weakest 10% RL) / mean MTTF$_{{DOR}}$(weakest 10% DOR)",
        fontsize=12,
    )

    # --- EM (links) ---
    ax_l.plot(xs, af_links, "o-", color="#e07b39", linewidth=2, markersize=5, label="LARE (RL)")
    ax_l.axhline(1.0, color="gray", linestyle="--", linewidth=1, label="DOR baseline (AF=1)")
    ax_l.set_xlabel("Injection Rate (Packet/Cycle/Node)", fontsize=11)
    ax_l.set_ylabel("AF (EM)", fontsize=11)
    ax_l.set_title("Link EM Wear (weakest 10%)")
    ax_l.legend(fontsize=9)
    ax_l.grid(True, alpha=0.3)
    ax_l.set_xlim(left=0)
    ax_l.set_ylim(bottom=0)

    # --- HCI (routers) ---
    ax_r.plot(xs, af_routers, "s-", color="#3a7ebf", linewidth=2, markersize=5, label="LARE (RL)")
    ax_r.axhline(1.0, color="gray", linestyle="--", linewidth=1, label="DOR baseline (AF=1)")
    ax_r.set_xlabel("Injection Rate (Packet/Cycle/Node)", fontsize=11)
    ax_r.set_ylabel("AF (HCI)", fontsize=11)
    ax_r.set_title("Router HCI Wear (weakest 10%)")
    ax_r.legend(fontsize=9)
    ax_r.grid(True, alpha=0.3)
    ax_r.set_xlim(left=0)
    ax_r.set_ylim(bottom=0)

    plt.tight_layout()
    outpath = os.path.join(OUTDIR, f"{traffic}.png")
    plt.savefig(outpath, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  Saved: {outpath}  ({len(xs)} rates)")

print("\nDone.")
