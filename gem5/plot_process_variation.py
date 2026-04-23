"""
Process variation heatmaps for an 8x8 mesh (Vth and Leff).

Mirrors GarnetNetwork.cc initialization exactly:
  - Die-normalised positions in [0,1]x[0,1]
  - Spherical correlation kernel C[i,j] = 1 - 1.5*(r/phi) + 0.5*(r/phi)^3
    for r < phi=0.5, else 0
  - Cholesky decomposition L s.t. L L^T = C
  - delta = z_rand * sigma_rand + (L z_sys) * sigma_sys
  - param[i] = nom + delta[i]
"""

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import TwoSlopeNorm

# ── Constants (match GarnetNetwork.hh) ────────────────────────────────────────
ISUB_VTH0  = 0.179       # V
ISUB_LEFF0 = 14.4e-9     # m  (display in nm)

PV_SIGMA_VTH_RAND  = 0.063
PV_SIGMA_VTH_SYS   = 0.063
PV_SIGMA_LEFF_RAND = 0.032
PV_SIGMA_LEFF_SYS  = 0.032
PV_PHI             = 0.5

RNG_SEED = 42
GRID     = 8
N        = GRID * GRID

# ── Die-normalised router positions ───────────────────────────────────────────
pos_x = np.array([(i % GRID) / (GRID - 1) for i in range(N)])
pos_y = np.array([(i // GRID) / (GRID - 1) for i in range(N)])

# ── Spherical correlation matrix ───────────────────────────────────────────────
dx = pos_x[:, None] - pos_x[None, :]
dy = pos_y[:, None] - pos_y[None, :]
r  = np.sqrt(dx**2 + dy**2)

t  = r / PV_PHI
C  = np.where(r < PV_PHI, 1.0 - 1.5*t + 0.5*t**3, 0.0)
np.fill_diagonal(C, 1.0)

# ── Cholesky decomposition ─────────────────────────────────────────────────────
C_reg = C + np.eye(N) * 1e-9
L = np.linalg.cholesky(C_reg)

# ── Generate PV samples ────────────────────────────────────────────────────────
rng = np.random.default_rng(RNG_SEED)

z_vth_r  = rng.standard_normal(N)
z_leff_r = rng.standard_normal(N)
z_vth_s  = rng.standard_normal(N)
z_leff_s = rng.standard_normal(N)

vth_sys  = L @ z_vth_s
leff_sys = L @ z_leff_s

sv_r = PV_SIGMA_VTH_RAND  * ISUB_VTH0
sv_s = PV_SIGMA_VTH_SYS   * ISUB_VTH0
sl_r = PV_SIGMA_LEFF_RAND * ISUB_LEFF0
sl_s = PV_SIGMA_LEFF_SYS  * ISUB_LEFF0

delta_vth  = z_vth_r  * sv_r + vth_sys  * sv_s
delta_leff = z_leff_r * sl_r + leff_sys * sl_s

vth_map  = (ISUB_VTH0  + delta_vth).reshape(GRID, GRID)               # V
leff_map = ((ISUB_LEFF0 + delta_leff) * 1e9).reshape(GRID, GRID)      # nm

# ── Plot ───────────────────────────────────────────────────────────────────────
fig, axes = plt.subplots(1, 2, figsize=(13, 5.5))
fig.suptitle("Within-Die Process Variation — 8x8 Mesh",
             fontsize=14, fontweight="bold", y=1.01)

def draw_heatmap(ax, data, nom, title, unit, cmap="RdYlGn_r"):
    vmin, vmax = data.min(), data.max()
    if vmin == nom or vmax == nom:
        norm = None
    else:
        norm = TwoSlopeNorm(vmin=vmin, vcenter=nom, vmax=vmax)
    im = ax.imshow(data, cmap=cmap, norm=norm, origin="upper",
                   interpolation="nearest", aspect="equal")

    cbar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
    cbar.set_label(f"{title} ({unit})", fontsize=10)
    cbar.ax.tick_params(labelsize=10)

    ax.set_title(title, fontsize=14, fontweight="bold")
    ax.set_xlabel("Column (X)", fontsize=13)
    ax.set_ylabel("Row (Y)", fontsize=13)
    ax.set_xticks(range(GRID))
    ax.set_yticks(range(GRID))
    ax.set_xticklabels([str(i) for i in range(GRID)], fontsize=12)
    ax.set_yticklabels([str(i) for i in range(GRID)], fontsize=12)
    ax.tick_params(length=0)

    for i in range(GRID + 1):
        ax.axhline(i - 0.5, color="white", linewidth=0.6)
        ax.axvline(i - 0.5, color="white", linewidth=0.6)

draw_heatmap(axes[0], vth_map,  ISUB_VTH0,       "Threshold Voltage (Vth)",     "V",  "RdYlGn_r")
draw_heatmap(axes[1], leff_map, ISUB_LEFF0*1e9,  "Effective Gate Length (Leff)", "nm", "RdYlGn_r")

# ── Summary stats ──────────────────────────────────────────────────────────────
# stats_text = (
#     f"Vth : nom={ISUB_VTH0:.3f} V | "
#     f"min={vth_map.min():.3f} V | max={vth_map.max():.3f} V | "
#     f"σ={vth_map.std():.4f} V\n"
#     f"Leff: nom={ISUB_LEFF0*1e9:.1f} nm | "
#     f"min={leff_map.min():.2f} nm | max={leff_map.max():.2f} nm | "
#     f"σ={leff_map.std():.3f} nm"
# )
# fig.text(0.5, -0.03, stats_text, ha="center", fontsize=12,
#          style="italic", color="#333333")

plt.tight_layout()
out_path = "plots/process_variation_map.png"
plt.savefig(out_path, dpi=150, bbox_inches="tight")
print(f"Saved → {out_path}")
plt.show()
