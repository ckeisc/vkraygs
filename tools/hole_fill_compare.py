#!/usr/bin/env python3
"""hole_fill_compare.py — Evaluate hole-filling variants against the Hyperscape flyby.

Batch-renders a Hyperscape SPZ from camera poses sampled along the capture
trajectory (approximating the flyby path) once per hole-filling variant, then
emits a self-contained comparison.html with the flyby frames as the reference
column.

Usage (Windows):
    python tools\\hole_fill_compare.py ^
        --spz C:\\scans\\garage.spz ^
        --poses C:\\scans\\garage_camera_poses ^
        --flyby C:\\scans\\flyby.mp4 ^
        --viewer build\\Release\\vkgs_viewer.exe ^
        --outdir hole_fill_out

Variants rendered (GS kernel):
    baseline (inflate 1.0), inflate-1.3, inflate-1.5
"""

import argparse
import base64
import html
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render_comparison import (load_colmap_poses, pick_pose_indices,
                               write_views_file)

try:
    from PIL import Image
    import numpy as np
except ImportError:
    sys.exit("needs Pillow and numpy: pip install pillow numpy")

VARIANTS = [
    ("baseline", ["--inflate", "1.0"]),
    ("inflate-1.3", ["--inflate", "1.3"]),
    ("inflate-1.5", ["--inflate", "1.5"]),
]


def extract_flyby_frames(flyby_mp4, outdir, count):
    os.makedirs(outdir, exist_ok=True)
    # Evenly spaced frames across the video via fps filter.
    probe = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration",
         "-of", "default=noprint_wrappers=1:nokey=1", flyby_mp4],
        capture_output=True, text=True)
    try:
        duration = float(probe.stdout.strip())
    except ValueError:
        duration = 12.0
    fps = count / duration
    cmd = ["ffmpeg", "-v", "error", "-y", "-i", flyby_mp4, "-vf",
           f"fps={fps:.4f},scale=1280:720",
           os.path.join(outdir, "ref_%02d.png")]
    print(f"[flyby] {' '.join(cmd)}", flush=True)
    subprocess.run(cmd, check=True)
    frames = sorted(f for f in os.listdir(outdir) if f.endswith(".png"))
    return [os.path.join(outdir, f) for f in frames[:count]]


def run_viewer(viewer, spz, views_file, outdir, prefix, extra_args, kernel="gs"):
    os.makedirs(outdir, exist_ok=True)
    cmd = [viewer, "-i", spz, "--kernel", kernel,
           "--views", views_file, "--outdir", outdir, "--prefix", prefix] + extra_args
    print(f"[render:{prefix}] {' '.join(cmd)}", flush=True)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    print(proc.stdout)
    if proc.returncode != 0:
        print(proc.stderr, file=sys.stderr)
        raise RuntimeError(f"vkgs_viewer ({prefix}) failed with code {proc.returncode}")
    n = sum(1 for _ in open(views_file))
    return [os.path.join(outdir, f"{prefix}_pose{i:03d}.png") for i in range(n)]


def hole_metrics(png_path):
    """Rough hole proxy: % of near-black pixels + mean brightness."""
    img = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.float32)
    dark = float(np.all(img < 16, axis=2).mean() * 100.0)
    mean = float(img.mean())
    return round(dark, 2), round(mean, 1)


def image_to_data_url(path):
    with open(path, "rb") as f:
        b64 = base64.b64encode(f.read()).decode("ascii")
    return f"data:image/png;base64,{b64}"


def write_html(report, out_path):
    scan = html.escape(report["scan"])
    variant_names = [v for v, _ in VARIANTS]
    rows = []
    for r in report["results"]:
        figs = [f"""
        <figure>
          <img src="{image_to_data_url(r['ref_abs'])}" alt="flyby reference">
          <figcaption>Hyperscape flyby (reference)</figcaption>
        </figure>"""]
        for v in variant_names:
            figs.append(f"""
        <figure>
          <img src="{image_to_data_url(r['renders'][v])}" alt="{v}">
          <figcaption>{v}<br>dark {r['metrics'][v][0]}% · mean {r['metrics'][v][1]}</figcaption>
        </figure>""")
        metric_cells = "".join(
            f"<td>{r['metrics'][v][0]}% / {r['metrics'][v][1]}</td>" for v in variant_names)
        rows.append(f"""
    <section class="pose">
      <h2>Pose {r['pose_index']}</h2>
      <div class="images">{''.join(figs)}</div>
      <table>
        <tr><th>Variant</th>{''.join(f'<th>{v}</th>' for v in variant_names)}</tr>
        <tr><td>Dark-pixel % / mean brightness</td>{metric_cells}</tr>
      </table>
    </section>""")

    doc = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Hole-filling vs Hyperscape flyby — {scan}</title>
<style>
  body {{ font-family: system-ui, sans-serif; max-width: 1600px; margin: 2rem auto; padding: 0 1rem; background: #111; color: #eee; }}
  h1 {{ font-size: 1.4rem; }}
  .pose {{ margin: 2rem 0; padding: 1rem; background: #1a1a1a; border-radius: 8px; }}
  .images {{ display: flex; gap: 0.8rem; overflow-x: auto; }}
  figure {{ flex: 1 0 300px; margin: 0; }}
  figure img {{ width: 100%; border-radius: 4px; }}
  figcaption {{ text-align: center; margin-top: 0.4rem; color: #aaa; font-size: 0.8rem; }}
  table {{ border-collapse: collapse; margin-top: 1rem; font-size: 0.85rem; }}
  th, td {{ border: 1px solid #444; padding: 0.4rem 0.8rem; text-align: left; }}
  th {{ background: #222; }}
  .note {{ color: #999; font-size: 0.85rem; margin-top: 2rem; }}
</style>
</head>
<body>
<h1>Hole-filling variants vs Hyperscape flyby — {scan}</h1>
<p>{len(report['results'])} poses sampled along the capture trajectory, GS kernel.</p>
{''.join(rows)}
<p class="note">Reference frames are evenly sampled from the flyby video and are
<em>approximately</em> matched, not pixel-aligned, with the rendered poses.
Dark-pixel % (all channels &lt; 16) is a rough hole proxy — holes read as
background. Visual judgment against the flyby column is the real metric.
See docs/hole-filling.md for the three hypotheses.</p>
</body>
</html>"""
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(doc)
    return out_path


def main():
    ap = argparse.ArgumentParser(description="Compare hole-filling variants vs Hyperscape flyby.")
    ap.add_argument("--spz", required=True)
    ap.add_argument("--poses", required=True, help="Hyperscape camera_poses file (JSON content)")
    ap.add_argument("--flyby", required=True, help="Hyperscape flyby .mp4")
    ap.add_argument("--viewer", required=True, help="path to vkgs_viewer executable")
    ap.add_argument("--outdir", default="hole_fill_out")
    ap.add_argument("--views-count", type=int, default=6)
    ap.add_argument("--kernel", default="gs")
    ap.add_argument("--variants", nargs="*", default=None,
                    help="subset of variant names to run (default: all)")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    poses_c2w, _ = load_colmap_poses(args.poses)
    indices = pick_pose_indices(len(poses_c2w), count=args.views_count)
    views_file = os.path.join(args.outdir, "views.txt")
    write_views_file(poses_c2w, indices, views_file)
    print(f"[views] {len(indices)} poses -> {views_file}")

    ref_frames = extract_flyby_frames(args.flyby, os.path.join(args.outdir, "reference"),
                                      len(indices))
    print(f"[flyby] {len(ref_frames)} reference frames")

    wanted = set(args.variants) if args.variants else None
    results = [{"pose_index": i, "ref_abs": ref_frames[i], "renders": {}, "metrics": {}}
               for i in range(len(indices))]
    for name, extra in VARIANTS:
        if wanted and name not in wanted:
            continue
        pngs = run_viewer(args.viewer, args.spz, views_file,
                          os.path.join(args.outdir, name), name, extra,
                          kernel=args.kernel)
        for i, p in enumerate(pngs):
            results[i]["renders"][name] = os.path.abspath(p)
            results[i]["metrics"][name] = hole_metrics(p)

    report = {"scan": os.path.basename(args.spz), "results": results}
    out = os.path.abspath(os.path.join(args.outdir, "comparison.html"))
    write_html(report, out)
    print(f"[done] {out}")


if __name__ == "__main__":
    main()
