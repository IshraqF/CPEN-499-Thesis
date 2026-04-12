#!/usr/bin/env python3
"""
Plot Acceleration Factor (AF = MTTF_algo / MTTF_DOR) for Aging-Aware RL
and Clotho-GAR, using the weakest 10% of components.

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


def mttf_paths(traffic, rate):
    rate_str = f"{float(rate):.2f}"
    dor_dir    = os.path.join(RESULTS, f"dor_{traffic}_rand_vnet{rate_str}")
    rl_dir     = os.path.join(RESULTS, f"rl_{traffic}_rand_vnet{rate_str}")
    clotho_name = CLOTHO_NAME[traffic]
    clotho_dir = os.path.join(RESULTS, f"clotho_{clotho_name}_rand_vnet{rate_str}")

    if traffic == "uniform":
        dor_file = os.path.join(dor_dir, f"mttf_dor_rand_vnet{rate_str}.txt")
        rl_file  = os.path.join(rl_dir,  f"mttf_rl_rand_vnet{rate_str}.txt")
    else:
        dor_file = os.path.join(dor_dir, f"mttf_dor_{traffic}_rand_vnet{rate_str}.txt")
        rl_file  = os.path.join(rl_dir,  f"mttf_rl_{traffic}_rand_vnet{rate_str}.txt")

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
    """AF = mean(algo MTTF of algo's weakest 10%) / mean(DOR MTTF of DOR's weakest 10%)"""
    floor = sum(1 for lid in algo_l if algo_l[lid][2] > 1e20)
    if floor == len(algo_l):
        return None, None

    top_l = max(1, round(0.10 * len(dor_l)))
    top_r = max(1, round(0.10 * len(dor_r)))

    dor_weak_l  = sorted(dor_l,  key=lambda k: dor_l[k][2])[:top_l]
    dor_weak_r  = sorted(dor_r,  key=dor_r.get)[:top_r]
    algo_weak_l = sorted(algo_l, key=lambda k: algo_l[k][2])[:top_l]
    algo_weak_r = sorted(algo_r, key=algo_r.get)[:top_r]

    mean_algo_l = sum(algo_l[l][2] for l in algo_weak_l) / top_l
    mean_dor_l  = sum(dor_l[l][2]  for l in dor_weak_l)  / top_l
    mean_algo_r = sum(algo_r[r]    for r in algo_weak_r)  / top_r
    mean_dor_r  = sum(dor_r[r]     for r in dor_weak_r)   / top_r

    return mean_algo_l / mean_dor_l, mean_algo_r / mean_dor_r


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

    xs_rl, af_rl_l, af_rl_r         = [], [], []
    xs_cl, af_cl_l, af_cl_r         = [], [], []

    for rate in rates[1:]:   # skip first injection rate
        dor_file, rl_file, clotho_file = mttf_paths(traffic, rate)

        if not os.path.exists(dor_file):
            continue
        try:
            dor_r, dor_l = parse_mttf(dor_file)
        except Exception as e:
            print(f"  Warning: DOR parse failed rate={rate}: {e}")
            continue

        # Aging-Aware RL
        if os.path.exists(rl_file):
            try:
                rl_r, rl_l = parse_mttf(rl_file)
                af_l, af_r = compute_af(dor_r, dor_l, rl_r, rl_l)
                if af_l is not None:
                    xs_rl.append(rate)
                    af_rl_l.append(af_l)
                    af_rl_r.append(af_r)
            except Exception as e:
                print(f"  Warning: RL parse failed rate={rate}: {e}")

        # Clotho-GAR
        if os.path.exists(clotho_file):
            try:
                cl_r, cl_l = parse_mttf(clotho_file)
                af_l, af_r = compute_af(dor_r, dor_l, cl_r, cl_l)
                if af_l is not None:
                    xs_cl.append(rate)
                    af_cl_l.append(af_l)
                    af_cl_r.append(af_r)
            except Exception as e:
                print(f"  Warning: Clotho parse failed rate={rate}: {e}")

    if not xs_rl and not xs_cl:
        print(f"  Skipping {traffic}: no valid data points")
        continue

    fig, (ax_l, ax_r) = plt.subplots(1, 2, figsize=(12, 4.5), sharey=False)
    fig.suptitle(
        f"Acceleration Factor — {traffic.replace('_', ' ').title()} Traffic\n"
        f"AF = mean MTTF$_{{algo}}$(weakest 10%) / mean MTTF$_{{DOR}}$(weakest 10%)",
        fontsize=12,
    )

    # --- EM (links) ---
    if xs_rl:
        ax_l.plot(xs_rl, af_rl_l, "o-", color="#e07b39", linewidth=2,
                  markersize=5, label="Aging-Aware RL")
    if xs_cl:
        ax_l.plot(xs_cl, af_cl_l, "^-.", color="#2ecc71", linewidth=2,
                  markersize=5, label="Clotho-GAR")
    ax_l.axhline(1.0, color="gray", linestyle="--", linewidth=1, label="DOR baseline (AF=1)")
    ax_l.set_xlabel("Injection Rate (Packet/Cycle/Node)", fontsize=11)
    ax_l.set_ylabel("AF (EM)", fontsize=11)
    ax_l.set_title("Link EM Wear (weakest 10%)")
    ax_l.legend(fontsize=9)
    ax_l.grid(True, alpha=0.3)
    ax_l.set_xlim(left=0)
    ax_l.set_ylim(bottom=0)

    # --- HCI (routers) ---
    if xs_rl:
        ax_r.plot(xs_rl, af_rl_r, "s-", color="#3a7ebf", linewidth=2,
                  markersize=5, label="Aging-Aware RL")
    if xs_cl:
        ax_r.plot(xs_cl, af_cl_r, "^-.", color="#9b59b6", linewidth=2,
                  markersize=5, label="Clotho-GAR")
    ax_r.axhline(1.0, color="gray", linestyle="--", linewidth=1, label="DOR baseline (AF=1)")
    ax_r.set_xlabel("Injection Rate (Packet/Cycle/Node)", fontsize=11)
    ax_r.set_ylabel("AF (HCI)", fontsize=11)
    ax_r.set_title("Router HCI Wear (weakest 10%)")
    ax_r.legend(fontsize=9)
    ax_r.grid(True, alpha=0.3)
    ax_r.set_xlim(left=0)
    ax_r.set_ylim(bottom=0)

    plt.tight_layout()
    outpath = os.path.join(OUTDIR, f"{traffic}_new.png")
    plt.savefig(outpath, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  Saved: {outpath}  (RL: {len(xs_rl)} rates, Clotho: {len(xs_cl)} rates)")

print("\nDone.")
