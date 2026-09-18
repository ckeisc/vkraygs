# Hole-filling

Goal: make vkraygs renders of Hyperscape SPZ captures look as solid as the
native Hyperscape viewer, which renders "holey" splat distributions without
visible holes. Reference: `frame_*.png` in this directory, extracted from the
Hyperscape flyby video (every 90th frame, 8 frames).

## Approach

**Bigger 3D volumes** — Hyperscape may render larger effective volumes per
primitive (not just 2D ellipse splats), closing gaps between neighbors.
Implemented as `--inflate F` (default 1.3): multiplies every Gaussian's 3D
sigma by F (applied as `+= log(F)` on the SPZ log-scales, in memory after
decode). `1.0` disables it.

Runs in `ApplyHoleFill()` (`src/vkgs/engine/splat_hole_fill.cc`) on the
decoded `spz::GaussianCloud`, after optional visibility-cluster culling and
before the GPU vertex buffer is built. Nothing is written to disk; the scene
is regenerated in memory on every load.

## Usage

```
vkgs_viewer.exe -i scan.spz                  # inflate 1.3 (default)
vkgs_viewer.exe -i scan.spz --inflate 1.0    # raw baseline, no hole-filling
vkgs_viewer.exe -i scan.spz --inflate 1.5    # stronger
```

## Evaluation

`tools/hole_fill_compare.py` (+ `.bat`) batch-renders inflate variants from
camera poses sampled along the capture trajectory (approximating the flyby
path), extracts matching flyby frames as the reference column, and emits a
self-contained `comparison.html`. A "% dark pixels" column serves as a rough
hole proxy — holes read as background-colored pixels. Visual judgment against
the flyby frames is the real metric.

## Status

- Validated on Windows (RTX 3080 Ti) against the garage SPZ: inflate 1.3
  closes holes with no visible downside; judged "almost perfect" vs the
  Hyperscape flyby.
- Densification and opacity-gamma variants were tested and removed (2026-09-18):
  individual A/B showed inflate doing all the work; the other two added
  nothing on top.
