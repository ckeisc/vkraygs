#!/usr/bin/env python3
"""
render_comparison.py — Render matched GS vs RayGS views of an SPZ scan.

Takes a Hyperscape-style .spz and its camera_poses file, converts the
camera poses to vkraygs view matrices, renders N poses with both the
GS (EWA) and RayGS kernels via the vkgs_viewer batch CLI, and computes
comparison metrics:

  - mean color shift (mean RGB Euclidean distance, 0-255 scale)
  - black-patch area (% of pixels with all channels < 16)
  - floater count (isolated bright components < 50 px)

Usage (Windows Command Prompt):
    python tools\\render_comparison.py ^
        --spz path\\to\\scan.spz ^
        --poses path\\to\\scan_camera_poses ^
        --viewer build\\Release\\vkgs_viewer.exe ^
        --outdir comparison_out

Requirements: Python 3.8+, numpy, Pillow. No scipy needed.
The vkgs_viewer binary must already be built from this branch.
"""

import argparse
import base64
import html
import json
import os
import subprocess
import sys

import numpy as np
from PIL import Image


# ----------------------------------------------------------------------------
# Pose handling
# ----------------------------------------------------------------------------

def load_colmap_poses(poses_path):
    """Load Hyperscape camera_poses file (JSON content, no extension).

    Returns (poses_c2w, intrinsics) where poses_c2w is (N,4,4) float64
    camera-to-world matrices.
    """
    with open(poses_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    # Prefer plain "input_poses" (list of 4x4 matrices). The "input_poses_v2"
    # variant wraps each pose in a {"pose": ..., "type": ..., "id": ...} dict.
    if "input_poses" in data and isinstance(data["input_poses"][0], list):
        raw = data["input_poses"]
    elif "input_poses_v2" in data:
        raw = [p["pose"] if isinstance(p, dict) else p
               for p in data["input_poses_v2"]]
    else:
        raise ValueError("no recognized pose list in " + poses_path)
    poses = np.asarray(raw, dtype=np.float64)
    assert poses.ndim == 3 and poses.shape[1:] == (4, 4), \
        f"unexpected pose shape {poses.shape}"
    return poses, data.get("intrinsics")


def c2w_to_view_and_eye(c2w):
    """Convert camera-to-world to vkraygs view matrix + eye position.

    view = inverse(c2w); eye = camera center in world = c2w translation.
    The views file wants the 4x4 view matrix in column-major order
    followed by the eye xyz.
    """
    view = np.linalg.inv(c2w)
    eye = c2w[:3, 3]
    return view, eye


def write_views_file(poses_c2w, indices, out_path):
    """Write batch views file: one line per pose.

    Each line: 16 floats (4x4 view matrix, column-major) + 3 floats (eye).
    """
    with open(out_path, "w") as f:
        for i in indices:
            view, eye = c2w_to_view_and_eye(poses_c2w[i])
            vals = list(view.T.reshape(-1)) + list(eye)  # column-major
            f.write(" ".join(f"{v:.6f}" for v in vals) + "\n")
    return out_path


def pick_pose_indices(n_poses, count=3):
    """Pick evenly spaced pose indices (first, middle, last by default)."""
    if n_poses <= count:
        return list(range(n_poses))
    return [int(round(i * (n_poses - 1) / (count - 1))) for i in range(count)]


def load_cluster_viewpoints(centroids_path):
    """Load cluster viewpoint positions from centroids JSON. Returns list of (x,y,z)."""
    with open(centroids_path) as f:
        data = json.load(f)
    # centroids JSON has 'views' list or similar; adapt to actual format
    views = data.get("views", data.get("viewpoints", []))
    return [(v[0], v[1], v[2]) for v in views]


def nearest_viewpoint(eye, viewpoints):
    """Find index of nearest viewpoint to camera position eye (x,y,z)."""
    best, best_d2 = 0, float("inf")
    for i, v in enumerate(viewpoints):
        d2 = (eye[0]-v[0])**2 + (eye[1]-v[1])**2 + (eye[2]-v[2])**2
        if d2 < best_d2:
            best, best_d2 = i, d2
    return best


# ----------------------------------------------------------------------------
# Rendering
# ----------------------------------------------------------------------------

def run_viewer(viewer, spz, kernel, views_file, outdir, prefix,
               cull_masks="", cull_view=-1):
    """Run vkgs_viewer batch CLI for one kernel. Returns list of PNG paths."""
    os.makedirs(outdir, exist_ok=True)
    cmd = [viewer, "-i", spz, "--kernel", kernel,
           "--views", views_file, "--outdir", outdir, "--prefix", prefix]
    if cull_masks and cull_view >= 0:
        cmd += ["--cull-masks", cull_masks, "--cull-view", str(cull_view)]
    print(f"[render] {' '.join(cmd)}", flush=True)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    print(proc.stdout)
    if proc.returncode != 0:
        print(proc.stderr, file=sys.stderr)
        raise RuntimeError(f"vkgs_viewer ({kernel}) failed with code {proc.returncode}")
    n = sum(1 for _ in open(views_file))
    return [os.path.join(outdir, f"{prefix}_pose{i:03d}.png") for i in range(n)]


# ----------------------------------------------------------------------------
# Metrics
# ----------------------------------------------------------------------------

def count_components(binary):
    """Count connected components (4-connectivity) via BFS on a bool array."""
    h, w = binary.shape
    labels = np.zeros((h, w), dtype=np.int32)
    count = 0
    sizes = []
    for y in range(h):
        for x in range(w):
            if binary[y, x] and labels[y, x] == 0:
                count += 1
                size = 0
                stack = [(y, x)]
                labels[y, x] = count
                while stack:
                    cy, cx = stack.pop()
                    size += 1
                    for ny, nx in ((cy - 1, cx), (cy + 1, cx),
                                   (cy, cx - 1), (cy, cx + 1)):
                        if 0 <= ny < h and 0 <= nx < w \
                                and binary[ny, nx] and labels[ny, nx] == 0:
                            labels[ny, nx] = count
                            stack.append((ny, nx))
                sizes.append(size)
    return count, sizes


def analyze_pair(gs_path, raygs_path):
    """Compute the three comparison metrics for one matched pose pair."""
    gs = np.asarray(Image.open(gs_path).convert("RGB"), dtype=np.float32)
    ray = np.asarray(Image.open(raygs_path).convert("RGB"), dtype=np.float32)
    assert gs.shape == ray.shape, "GS/RayGS image size mismatch"

    # 1. mean color shift: mean Euclidean RGB distance (0-255 scale)
    mean_shift = float(np.sqrt(np.sum((gs - ray) ** 2, axis=2)).mean())

    # 2. black-patch area: % of pixels with all channels < 16
    gs_black = float(np.all(gs < 16, axis=2).mean() * 100.0)
    ray_black = float(np.all(ray < 16, axis=2).mean() * 100.0)

    # 3. floater count: isolated bright components (< 50 px) on a
    #    half-resolution image (full-res BFS is slow in pure Python)
    results = {}
    for name, img in (("gs", gs), ("raygs", ray)):
        small = np.asarray(Image.fromarray(img.astype(np.uint8))
                           .resize((img.shape[1] // 2, img.shape[0] // 2)))
        bright = np.all(small > 180, axis=2)
        _, sizes = count_components(bright)
        results[f"{name}_floaters"] = int(sum(1 for s in sizes if s < 50))
        results[f"{name}_bright_components"] = len(sizes)

    return {
        "mean_rgb_shift": round(mean_shift, 2),
        "gs_black_pct": round(gs_black, 2),
        "raygs_black_pct": round(ray_black, 2),
        **results,
    }


# ----------------------------------------------------------------------------
# HTML report (single file, images as inline data URLs)
# ----------------------------------------------------------------------------

def image_to_data_url(path):
    with open(path, "rb") as f:
        b64 = base64.b64encode(f.read()).decode("ascii")
    return f"data:image/png;base64,{b64}"


def write_html_report(report, out_path):
    """Write a single self-contained HTML file with inline images + stats."""
    scan = html.escape(report["scan"])
    rows = []
    for r in report["results"]:
        gs_url = image_to_data_url(r["gs_abs"])
        ray_url = image_to_data_url(r["raygs_abs"])
        rows.append(f"""
    <section class="pose">
      <h2>Pose {r['pose_index']}</h2>
      <div class="images">
        <figure>
          <img src="{gs_url}" alt="GS render pose {r['pose_index']}">
          <figcaption>GS (EWA)</figcaption>
        </figure>
        <figure>
          <img src="{ray_url}" alt="RayGS render pose {r['pose_index']}">
          <figcaption>RayGS</figcaption>
        </figure>
      </div>
      <table>
        <tr><th>Metric</th><th>GS</th><th>RayGS</th></tr>
        <tr><td>Mean color shift vs other kernel</td><td colspan="2">{r['mean_rgb_shift']} / 255</td></tr>
        <tr><td>Black-patch area</td><td>{r['gs_black_pct']}%</td><td>{r['raygs_black_pct']}%</td></tr>
        <tr><td>Floater count (isolated bright comps)</td><td>{r['gs_floaters']}</td><td>{r['raygs_floaters']}</td></tr>
      </table>
    </section>""")

    doc = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>GS vs RayGS — {scan}</title>
<style>
  body {{ font-family: system-ui, sans-serif; max-width: 1400px; margin: 2rem auto; padding: 0 1rem; background: #111; color: #eee; }}
  h1 {{ font-size: 1.4rem; }}
  .pose {{ margin: 2rem 0; padding: 1rem; background: #1a1a1a; border-radius: 8px; }}
  .images {{ display: flex; gap: 1rem; }}
  figure {{ flex: 1; margin: 0; }}
  figure img {{ width: 100%; border-radius: 4px; }}
  figcaption {{ text-align: center; margin-top: 0.4rem; color: #aaa; }}
  table {{ border-collapse: collapse; margin-top: 1rem; }}
  th, td {{ border: 1px solid #444; padding: 0.4rem 0.8rem; text-align: left; }}
  th {{ background: #222; }}
  .note {{ color: #999; font-size: 0.85rem; margin-top: 2rem; }}
</style>
</head>
<body>
<h1>GS (EWA) vs RayGS — {scan}</h1>
<p>{len(report['results'])} matched poses rendered with vkraygs batch CLI.</p>
{''.join(rows)}
<p class="note">Metrics: mean color shift = mean Euclidean RGB distance (0&ndash;255)
between the two kernels; black-patch area = % of pixels with all channels &lt; 16;
floater count = isolated bright components (&lt; 50 px) at half resolution.
Sorting state depends on the renderer build (VKGS_SKIP_SORT bypasses the radix sort).</p>
</body>
</html>"""
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(doc)
    return out_path


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="Render matched GS vs RayGS views of an SPZ scan "
                    "and compute comparison metrics.")
    ap.add_argument("--spz", required=True, help="input .spz scan")
    ap.add_argument("--poses", required=True,
                    help="Hyperscape camera_poses file (JSON content)")
    ap.add_argument("--viewer", required=True,
                    help="vkgs_viewer binary (built from this branch)")
    ap.add_argument("--outdir", required=True, help="output directory")
    ap.add_argument("--num-poses", type=int, default=3,
                    help="how many evenly spaced poses to render (default 3)")
    ap.add_argument("--pose-indices", default="",
                    help="comma-separated pose indices (overrides --num-poses)")
    ap.add_argument("--cull-masks", default="",
                    help="cluster_masks.bin for visibility culling (optional)")
    ap.add_argument("--cull-centroids", default="",
                    help="cluster_centroids.json (required with --cull-masks)")
    args = ap.parse_args()

    poses_c2w, intrinsics = load_colmap_poses(args.poses)
    n = len(poses_c2w)
    print(f"[poses] loaded {n} camera poses from {args.poses}")
    if intrinsics:
        print(f"[poses] intrinsics sample: {str(intrinsics)[:120]}")

    if args.pose_indices:
        indices = [int(s) for s in args.pose_indices.split(",")]
    else:
        indices = pick_pose_indices(n, args.num_poses)
    print(f"[poses] rendering pose indices: {indices}")

    os.makedirs(args.outdir, exist_ok=True)
    views_file = os.path.join(args.outdir, "views.txt")
    write_views_file(poses_c2w, indices, views_file)
    print(f"[views] wrote {views_file}")

    # Visibility culling: compute nearest viewpoint per pose if enabled.
    cull_views = {}
    if args.cull_masks:
        if not args.cull_centroids:
            raise SystemExit("--cull-centroids required with --cull-masks")
        viewpoints = load_cluster_viewpoints(args.cull_centroids)
        print(f"[cull] loaded {len(viewpoints)} viewpoints from {args.cull_centroids}")
        for pi in indices:
            _, eye = c2w_to_view_and_eye(poses_c2w[pi])
            vi = nearest_viewpoint(eye, viewpoints)
            cull_views[pi] = vi
            print(f"[cull] pose {pi}: eye={tuple(round(x,3) for x in eye)} -> view {vi}")

    gs_dir = os.path.join(args.outdir, "gs")
    raygs_dir = os.path.join(args.outdir, "raygs")
    if cull_views:
        # Per-pose rendering: each pose gets its own viewpoint culling.
        gs_paths, raygs_paths = [], []
        for j, pi in enumerate(indices):
            vi = cull_views[pi]
            pvf = os.path.join(args.outdir, f"views_pose{pi:03d}.txt")
            write_views_file(poses_c2w, [pi], pvf)
            gp = run_viewer(args.viewer, args.spz, "gs", pvf, gs_dir,
                            f"gs_pose{j:03d}", args.cull_masks, vi)[0]
            # Rename to expected pattern for report
            gp_final = os.path.join(gs_dir, f"gs_pose{j:03d}.png")
            os.rename(gp, gp_final) if gp != gp_final else None
            rp = run_viewer(args.viewer, args.spz, "raygs", pvf, raygs_dir,
                            f"raygs_pose{j:03d}", args.cull_masks, vi)[0]
            rp_final = os.path.join(raygs_dir, f"raygs_pose{j:03d}.png")
            os.rename(rp, rp_final) if rp != rp_final else None
            gs_paths.append(gp_final)
            raygs_paths.append(rp_final)
    else:
        gs_paths = run_viewer(args.viewer, args.spz, "gs", views_file, gs_dir, "gs")
        raygs_paths = run_viewer(args.viewer, args.spz, "raygs",
                                 views_file, raygs_dir, "raygs")

    report = {"scan": os.path.basename(args.spz),
              "pose_indices": indices,
              "results": []}
    print("\n=== metrics ===")
    for pi, gp, rp in zip(indices, gs_paths, raygs_paths):
        m = analyze_pair(gp, rp)
        m["pose_index"] = pi
        m["gs_image"] = os.path.relpath(gp, args.outdir)
        m["raygs_image"] = os.path.relpath(rp, args.outdir)
        m["gs_abs"] = os.path.abspath(gp)
        m["raygs_abs"] = os.path.abspath(rp)
        report["results"].append(m)
        print(f"pose {pi}: shift={m['mean_rgb_shift']}/255 "
              f"black gs={m['gs_black_pct']}% raygs={m['raygs_black_pct']}% "
              f"floaters gs={m['gs_floaters']} raygs={m['raygs_floaters']}")

    report_path = os.path.join(args.outdir, "metrics.json")
    with open(report_path, "w") as f:
        json.dump({k: v for k, v in report.items()},
                  f, indent=2, default=str)
    print(f"\n[done] metrics -> {report_path}")

    html_path = os.path.join(args.outdir, "comparison.html")
    write_html_report(report, html_path)
    print(f"[done] report -> {html_path} (open in a browser)")
    print(f"[done] images -> {gs_dir} , {raygs_dir}")


if __name__ == "__main__":
    main()
