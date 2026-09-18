# vkraygs (Hyperscape fork)

Purpose of this fork: load and render Meta Hyperscape SPZ captures (Quest 3 scans)
with visual quality matching the native Hyperscape viewer, as part of an open
capture-to-splat-to-VR workflow without cloud lock-in.

## Hyperscape data loading changes

- **Direct `.spz` loading** (drag-and-drop and CLI) via `third_party/spz`.
- **Sidecar auto-detection**: Hyperscape visibility data (`_cluster_centroids.json`,
  `_cluster_masks.bin`) is picked up automatically when placed next to the `.spz`.
- **DC-only rendering is the default.** Hyperscape SPZ files use a non-standard SH
  convention that produces black patches and wrong colors under standard SH evaluation;
  ignoring the SH rest terms fixes this. `--full-sh` opts back into standard SH for
  conventional 3DGS files.
- **sRGB encoding** applied in the splat fragment shaders (the swapchain is UNORM).
- **Z-up is the default** (Hyperscape captures are Z-up); `--y-up` opts out.
- **Visibility-cluster culling**: GPU culling driven by Hyperscape's per-capture
  visibility clusters (`--cull-masks`, `--cull-view`), including a dynamic mode using
  the union of the 3 nearest viewpoints.
- **Opacity tools**:
  - `--opacity-bias`: logit-space opacity boost, adjustable live with PageUp/PageDown
    (applied GPU-side; the PLY buffer is kept alive for re-dispatch).
  - `--alpha-scale` / `--alpha-bias` (experimental): applies
    `alpha_out = clamp(alpha * scale + bias, 0, 1)` in the parse shader, modeled on
    the native renderer's `alphaCorrectionParams` uniform block.
    See `docs/hyperscape-opacity-trace.md` for the binary trace — the exact native
    formula was not recoverable (it lives in the unavailable shader), so these are
    tunable approximations, not a reverse-engineered match.
- **Kernel and camera fixes**: separate GS/RayGS instance buffers (fixes
  RayGS→GS corruption), geometry-shader vertex layout fix, `Camera::Dolly` free
  fly-through navigation.
- **Batch rendering**: `--kernel gs|raygs`, `--views`, `--outdir` for scripted
  multi-view renders.
- **Comparison tooling**: `tools/render_comparison.py` (+ `.bat`) produces matched-pose
  GS-vs-RayGS reports with flyby reference frames.
