"""
Steady-state temperature heatmap for an 8x8 Garnet mesh.

Mirrors GarnetNetwork.cc initialization exactly:
  - Die-normalised positions in [0,1]x[0,1]
  - Spherical correlation kernel C[i,j] = 1 - 1.5*(r/phi) + 0.5*(r/phi)^3
    for r < phi=0.5, else 0
  - Cholesky decomposition L s.t. L L^T = C
  - T[i] = clamp(TEMP_MEAN_K + TEMP_STD_K * (L z)[i], TEMP_MIN_K, TEMP_MAX_K)
"""

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap

# ── Constants (match GarnetNetwork.hh) ────────────────────────────────────────
TEMP_MIN_K  = 338.0
TEMP_MAX_K  = 358.0
TEMP_MEAN_K = 348.0
TEMP_STD_K  = 3.3
PV_PHI      = 0.5

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
# Add small regularisation (matches the +1e-12 in C++)
C_reg = C + np.eye(N) * 1e-9
L = np.linalg.cholesky(C_reg)   # lower-triangular, L @ L.T = C_reg

# ── Temperature map ────────────────────────────────────────────────────────────
rng   = np.random.default_rng(RNG_SEED)
z_t   = rng.standard_normal(N)
corr  = L @ z_t                                    # spatially correlated draw
temp_K = np.clip(TEMP_MEAN_K + TEMP_STD_K * corr, TEMP_MIN_K, TEMP_MAX_K)

temp_C_map = (temp_K - 273.15).reshape(GRID, GRID)   # display in °C
temp_K_map = temp_K.reshape(GRID, GRID)

# ── Colormap: blue (cool) → yellow → red (hot) ────────────────────────────────
temp_cmap = LinearSegmentedColormap.from_list(
    "temp",
    ["#2166ac", "#74add1", "#ffffbf", "#f46d43", "#a50026"],
    N=256
)

# ── Plot ───────────────────────────────────────────────────────────────────────
fig, ax = plt.subplots(figsize=(7, 6))
fig.suptitle("Temperature Map — 8x8 Mesh",
             fontsize=13, fontweight="bold")

im = ax.imshow(temp_C_map, cmap=temp_cmap, origin="upper",
               interpolation="nearest", aspect="equal",
               vmin=TEMP_MIN_K - 273.15, vmax=TEMP_MAX_K - 273.15)

cbar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
cbar.set_label("Temperature Tj (°C)", fontsize=10)
cbar.ax.tick_params(labelsize=8)

ax.set_xlabel("Column (X)", fontsize=10)
ax.set_ylabel("Row (Y)", fontsize=10)
ax.set_xticks(range(GRID))
ax.set_yticks(range(GRID))
ax.set_xticklabels([str(i) for i in range(GRID)], fontsize=8)
ax.set_yticklabels([str(i) for i in range(GRID)], fontsize=8)
ax.tick_params(length=0)

for i in range(GRID + 1):
    ax.axhline(i - 0.5, color="white", linewidth=0.6, alpha=0.5)
    ax.axvline(i - 0.5, color="white", linewidth=0.6, alpha=0.5)

# ── Summary stats ──────────────────────────────────────────────────────────────
# t_min, t_max = temp_C_map.min(), temp_C_map.max()
# stats_text = (
#     f"Spherical correlation  φ={PV_PHI}  |  "
#     f"Nominal {TEMP_MEAN_K-273.15:.1f} °C  |  "
#     f"σ={TEMP_STD_K:.1f} K  |  "
#     f"Range [{TEMP_MIN_K-273.15:.0f}, {TEMP_MAX_K-273.15:.0f}] °C\n"
#     f"Observed: min={t_min:.1f} °C  |  max={t_max:.1f} °C  |  "
#     f"ΔT={t_max-t_min:.1f} °C  |  σ={temp_C_map.std():.2f} °C"
# )
# fig.text(0.5, -0.03, stats_text, ha="center", fontsize=9,
#          style="italic", color="#333333")

plt.tight_layout()
out_path = "plots/temperature_map.png"
plt.savefig(out_path, dpi=150, bbox_inches="tight")
print(f"Saved → {out_path}")
plt.show()
