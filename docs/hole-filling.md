# Hole-filling experiments

Goal: make vkraygs renders of Hyperscape SPZ captures look as solid as the
native Hyperscape viewer, which renders "holey" splat distributions without
visible holes. Reference: `frame_*.png` in this directory, extracted from the
Hyperscape flyby video (every 90th frame, 8 frames).

## Hypotheses (from Joe)

1. **Bigger 3D volumes** — Hyperscape may render larger effective volumes per
   primitive (not just 2D ellipse splats), closing gaps between neighbors.
   Implemented as `--inflate F`: multiplies every Gaussian's 3D sigma by F
   (applied as `+= log(F)` on the SPZ log-scales, in memory after decode).
2. **Dynamic densification** — where the capture has enough Gaussians to imply
   a surface but not enough to be solid, interpolate new Gaussians between
   neighbors. Implemented as `--densify [--densify-gap G]`: a spatial-hash
   pass inserts a midpoint splat (lerped position/scale/rotation/color/opacity,
   averaged SH) wherever the nearest-neighbor gap exceeds `G * (r_i + r_j)`
   with `r` the 1-sigma radius. One pass, capped at doubling the splat count.
3. **Opacity ramp** — real surfaces are rarely semi-transparent in small
   patches. Implemented as `--opacity-gamma G`: `o' = o^(1/G)` in opacity
   space (logit -> sigmoid -> gamma -> logit), so `G > 1` pushes mid
   opacities toward 1. Smooth curve, not a hard threshold.

All three run in `ApplyHoleFill()` (`src/vkgs/engine/splat_hole_fill.cc`) on
the decoded `spz::GaussianCloud`, after optional visibility-cluster culling
and before the GPU vertex buffer is built. Nothing is written to disk; the
scene is regenerated in memory on every load.

## Suggested starting points

```
vkgs_viewer.exe -i scan.spz --inflate 1.3
vkgs_viewer.exe -i scan.spz --densify
vkgs_viewer.exe -i scan.spz --opacity-gamma 2.0
vkgs_viewer.exe -i scan.spz --inflate 1.3 --densify --opacity-gamma 2.0
```

## Evaluation

`tools/hole_fill_compare.py` (+ `.bat`) batch-renders each variant from
camera poses sampled along the capture trajectory (approximating the flyby
path), extracts matching flyby frames as the reference column, and emits a
self-contained `comparison.html`. A "% dark pixels" column serves as a rough
hole proxy — holes read as background-colored pixels. Visual judgment against
the flyby frames is the real metric.

## Status / caveats

- CPU-validated on the garage SPZ (372,938 splats): inflate 1.3 scales mean
  sigma exactly 1.3x; gamma 2.0 lifts mean opacity 0.695 -> 0.805; densify
  (gap 3.0) adds 7,056 midpoint splats in ~3 s total load time; all-off is a
  verified no-op.
- Not GPU-verified on this VM (no Vulkan device); Windows build + visual
  comparison is the real test.
- Densify is deliberately conservative (original splats only as seeds, one
  pass, 2x cap) to avoid creating floaters in empty space.
