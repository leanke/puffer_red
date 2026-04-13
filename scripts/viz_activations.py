#!/usr/bin/env python3
"""Visualize CNN activation heatmaps from captured policy data.

Supports both legacy JSONL format and compact .npz chunk format.

Usage:
    # Single frame (auto-detects format)
    python scripts/viz_activations.py viz_data/ --step 500
    python scripts/viz_activations.py viz_data/captures.jsonl --step 500

    # Generate activation video/gif from npz captures
    python scripts/viz_activations.py viz_data/ --video activations.gif --fps 30
    python scripts/viz_activations.py viz_data/ --video activations.mp4 --fps 60

    # Limit video to a step range
    python scripts/viz_activations.py viz_data/ --video out.gif --start 1000 --end 2000

    # Channel statistics over time
    python scripts/viz_activations.py viz_data/ --stats

    # Legacy options
    python scripts/viz_activations.py viz_data/captures.jsonl --top-k 8 --show
"""

import argparse
import glob as globmod
import json
import os
import sys

import numpy as np

_plt = None
def get_plt(interactive=False):
    global _plt
    if _plt is None:
        import matplotlib
        if not interactive:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        _plt = plt
    return _plt


# ── Loaders ───────────────────────────────────────────────────────────────

def load_jsonl(path, target_step=None):
    """Load captures from legacy JSONL file."""
    captures = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if target_step is not None:
                if rec["step"] == target_step:
                    return [rec]
            else:
                captures.append(rec)
    return captures


def load_npz_dir(dir_path):
    """Load all .npz chunks from a capture directory, concatenated.

    Returns a dict of numpy arrays keyed by field name, or None if
    no chunks found.
    """
    chunks = sorted(globmod.glob(os.path.join(dir_path, "chunk_*.npz")))
    if not chunks:
        return None
    combined = {}
    for chunk_path in chunks:
        with np.load(chunk_path) as data:
            for key in data.files:
                combined.setdefault(key, []).append(data[key])
    for key in combined:
        combined[key] = np.concatenate(combined[key])
    return combined


def npz_record_to_dict(data, idx):
    """Convert one npz record at index to a dict matching legacy format."""
    rec = {
        "step": int(data["steps"][idx]),
        "screen": data["screens"][idx],
        "cnn_activations": {},
    }
    if "values" in data:
        rec["value"] = float(data["values"][idx])
    for name in ["conv1", "conv2", "conv3"]:
        full_key = f"{name}_full"
        spatial_key = f"{name}_spatial"
        if full_key in data:
            act = data[full_key][idx]
            rec["cnn_activations"][name] = {
                "shape": list(act.shape),
                "data": act,
            }
        elif spatial_key in data:
            hm = data[spatial_key][idx]
            rec["cnn_activations"][name] = {
                "shape": [1, *hm.shape],
                "data": hm[np.newaxis],
            }
    return rec


def auto_load(path, target_step=None):
    """Auto-detect format and load captures.

    Returns:
        (records, npz_data) — one of them will be set:
        - For JSONL: (list_of_dicts, None)
        - For npz:   (None, dict_of_arrays)
    """
    if os.path.isfile(path) and path.endswith(".jsonl"):
        return load_jsonl(path, target_step), None

    if os.path.isdir(path):
        npz = load_npz_dir(path)
        if npz is not None:
            if target_step is not None:
                idx = np.searchsorted(npz["steps"], target_step)
                if idx < len(npz["steps"]) and npz["steps"][idx] == target_step:
                    return [npz_record_to_dict(npz, idx)], npz
                print(f"Step {target_step} not found in npz data. "
                      f"Available range: {npz['steps'][0]}–{npz['steps'][-1]}",
                      file=sys.stderr)
                return [], npz
            return None, npz
        # Fall back to JSONL in directory
        jsonl = os.path.join(path, "captures.jsonl")
        if os.path.isfile(jsonl):
            return load_jsonl(jsonl, target_step), None

    print(f"No captures found at {path}", file=sys.stderr)
    return [], None


# ── Single-frame plotting ─────────────────────────────────────────────────

def plot_activations(capture, top_k=None, output=None, show=False):
    """Plot CNN activations for a single capture."""
    plt = get_plt(interactive=show)

    screen = np.asarray(capture["screen"], dtype=np.uint8)
    cnn_acts = capture["cnn_activations"]

    layer_names = ["conv1", "conv2", "conv3"]
    layers = [(name, cnn_acts[name]) for name in layer_names if name in cnn_acts]

    n_layers = len(layers)
    fig_height = 3 + 3 * n_layers
    fig, axes = plt.subplots(n_layers + 1, 1, figsize=(14, fig_height),
                             gridspec_kw={"height_ratios": [2] + [3] * n_layers})
    fig.suptitle(f"CNN Activations — step {capture['step']} "
                 f"(phase: {capture.get('phase', '?')})", fontsize=14)

    ax_screen = axes[0]
    ax_screen.imshow(screen, cmap="gray", vmin=0, vmax=255, aspect="auto")
    ax_screen.set_title("Input Screen (72×80 grayscale)")
    ax_screen.axis("off")

    for i, (name, layer_data) in enumerate(layers):
        ax = axes[i + 1]
        shape = layer_data["shape"]
        data = np.asarray(layer_data["data"])
        n_channels = shape[0]

        if top_k and top_k < n_channels:
            channel_energy = data.reshape(n_channels, -1).mean(axis=1)
            top_indices = np.argsort(channel_energy)[-top_k:][::-1]
            data = data[top_indices]
            n_channels = top_k
            title_extra = f" (top {top_k} by mean activation)"
        else:
            top_indices = None
            title_extra = ""

        ncols = min(16, n_channels)
        nrows = int(np.ceil(n_channels / ncols))
        h, w = shape[1], shape[2]

        grid = np.zeros((nrows * h, ncols * w))
        for ch_idx in range(n_channels):
            r, c = divmod(ch_idx, ncols)
            grid[r * h:(r + 1) * h, c * w:(c + 1) * w] = data[ch_idx]

        im = ax.imshow(grid, cmap="inferno", aspect="auto")
        ax.set_title(f"{name}: {shape[0]} channels × {shape[1]}×{shape[2]}{title_extra}",
                     fontsize=10)
        ax.axis("off")
        plt.colorbar(im, ax=ax, fraction=0.02, pad=0.01)

    plt.tight_layout()

    if output:
        plt.savefig(output, dpi=150, bbox_inches="tight")
        print(f"Saved to {output}")
    if show:
        plt.show()
    if not show and not output:
        default_out = f"activations_step{capture['step']}.png"
        plt.savefig(default_out, dpi=150, bbox_inches="tight")
        print(f"Saved to {default_out}")
    plt.close(fig)


# ── Channel statistics ────────────────────────────────────────────────────

def plot_channel_stats(captures, output=None, show=False):
    """Plot per-channel activation statistics across all captures."""
    plt = get_plt(interactive=show)

    layer_names = ["conv1", "conv2", "conv3"]
    fig, axes = plt.subplots(1, len(layer_names), figsize=(18, 5))
    fig.suptitle("Per-Channel Mean Activation Over Time", fontsize=14)

    for ax, name in zip(axes, layer_names):
        means_over_time = []
        steps = []
        for cap in captures:
            if name not in cap.get("cnn_activations", {}):
                continue
            data = np.asarray(cap["cnn_activations"][name]["data"])
            channel_means = data.reshape(data.shape[0], -1).mean(axis=1)
            means_over_time.append(channel_means)
            steps.append(cap["step"])

        if not means_over_time:
            ax.set_title(f"{name}: no data")
            continue

        means_arr = np.array(means_over_time)
        im = ax.imshow(means_arr.T, aspect="auto", cmap="viridis",
                       extent=[steps[0], steps[-1], means_arr.shape[1], 0])
        ax.set_xlabel("Step")
        ax.set_ylabel("Channel")
        ax.set_title(f"{name} ({means_arr.shape[1]} channels)")
        plt.colorbar(im, ax=ax, fraction=0.03)

    plt.tight_layout()

    out = output or "channel_stats.png"
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved channel stats to {out}")
    if show:
        plt.show()
    plt.close(fig)


# ── Video / GIF generation ───────────────────────────────────────────────

def generate_video(data, output_path, fps=30, dpi=100,
                   start_step=None, end_step=None):
    """Generate video or gif from npz capture data.

    Shows the game screen alongside per-layer spatial activation heatmaps.
    Requires 'screens' and '*_spatial' arrays (light or full mode captures).
    For .gif output uses pillow writer; for .mp4 uses ffmpeg.
    """
    plt = get_plt(interactive=False)
    from matplotlib.animation import FuncAnimation

    # Apply step range filter
    mask = np.ones(len(data["steps"]), dtype=bool)
    if start_step is not None:
        mask &= data["steps"] >= start_step
    if end_step is not None:
        mask &= data["steps"] <= end_step
    indices = np.where(mask)[0]

    if len(indices) == 0:
        print("No frames in the specified step range.", file=sys.stderr)
        return

    spatial_keys = [f"conv{i}_spatial" for i in [1, 2, 3]
                    if f"conv{i}_spatial" in data]
    n_cols = 1 + len(spatial_keys)
    fig, axes = plt.subplots(1, n_cols, figsize=(4 * n_cols, 4))
    if n_cols == 1:
        axes = [axes]

    # Initialize screen
    first = indices[0]
    screen_im = axes[0].imshow(data["screens"][first], cmap="gray",
                               vmin=0, vmax=255, aspect="equal")
    axes[0].set_title("Screen")
    axes[0].axis("off")

    # Initialize heatmaps with consistent color scale
    heat_ims = []
    for i, key in enumerate(spatial_keys):
        subset = data[key][indices]
        vmax = max(float(np.percentile(subset, 99)), 1e-6)
        im = axes[i + 1].imshow(data[key][first], cmap="inferno",
                                vmin=0, vmax=vmax, aspect="equal")
        axes[i + 1].set_title(key.replace("_spatial", ""))
        axes[i + 1].axis("off")
        heat_ims.append(im)

    title = fig.suptitle("")
    plt.tight_layout(rect=[0, 0, 1, 0.93])

    def update(frame_num):
        idx = indices[frame_num]
        screen_im.set_data(data["screens"][idx])
        txt = f"Step {data['steps'][idx]}"
        if "values" in data:
            txt += f"  V={data['values'][idx]:.2f}"
        title.set_text(txt)
        for im, key in zip(heat_ims, spatial_keys):
            im.set_data(data[key][idx])
        return [screen_im, title] + heat_ims

    anim = FuncAnimation(fig, update, frames=len(indices),
                         interval=1000 / fps, blit=False)

    writer = "pillow" if output_path.endswith(".gif") else "ffmpeg"
    print(f"Rendering {len(indices)} frames to {output_path} "
          f"({writer}, {fps} fps) ...")
    anim.save(output_path, writer=writer, fps=fps, dpi=dpi)
    plt.close(fig)
    size_mb = os.path.getsize(output_path) / 1024 / 1024
    print(f"Saved {output_path} ({size_mb:.1f} MB)")


# ── CLI ───────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Visualize CNN activations from policy captures.")
    parser.add_argument("captures_path",
                        help="Path to captures.jsonl file or npz capture directory")
    parser.add_argument("--step", type=int, default=None,
                        help="Specific step to visualize (default: latest)")
    parser.add_argument("--top-k", type=int, default=None,
                        help="Show only top-K most active channels per layer")
    parser.add_argument("--output", "-o", default=None,
                        help="Output image path (for single-frame mode)")
    parser.add_argument("--show", action="store_true",
                        help="Show interactive plot window")
    parser.add_argument("--stats", action="store_true",
                        help="Plot channel statistics over all captures")
    parser.add_argument("--video", default=None, metavar="PATH",
                        help="Generate video/gif (e.g., --video out.gif or out.mp4)")
    parser.add_argument("--fps", type=int, default=30,
                        help="Frames per second for video output (default: 30)")
    parser.add_argument("--dpi", type=int, default=100,
                        help="DPI for video output (default: 100)")
    parser.add_argument("--start", type=int, default=None, metavar="STEP",
                        help="Start step for video range")
    parser.add_argument("--end", type=int, default=None, metavar="STEP",
                        help="End step for video range")
    args = parser.parse_args()

    # Video mode — requires npz data
    if args.video:
        records, npz_data = auto_load(args.captures_path)
        if npz_data is None:
            # Try loading JSONL records into npz-like arrays for video
            if records:
                print("Video generation requires npz format captures. "
                      "Re-run training with the compact capture format.",
                      file=sys.stderr)
            else:
                print(f"No captures found at {args.captures_path}",
                      file=sys.stderr)
            sys.exit(1)
        generate_video(npz_data, args.video, fps=args.fps, dpi=args.dpi,
                       start_step=args.start, end_step=args.end)
        return

    # Single-frame or stats mode
    records, npz_data = auto_load(args.captures_path, target_step=args.step)

    if args.stats:
        if npz_data is not None and records is None:
            # Convert all npz records to dicts for stats
            records = [npz_record_to_dict(npz_data, i)
                       for i in range(len(npz_data["steps"]))]
        if not records:
            print("No captures found.", file=sys.stderr)
            sys.exit(1)
        plot_channel_stats(records, output=args.output, show=args.show)
        return

    # Single-frame visualization
    if npz_data is not None and not records:
        # No specific step requested; use latest
        records = [npz_record_to_dict(npz_data, -1)]
    if not records:
        print(f"No captures found at {args.captures_path}"
              + (f" for step {args.step}" if args.step else ""),
              file=sys.stderr)
        sys.exit(1)

    capture = records[-1]
    print(f"Visualizing step {capture['step']} "
          f"(phase: {capture.get('phase', '?')})")
    plot_activations(capture, top_k=args.top_k,
                     output=args.output, show=args.show)


if __name__ == "__main__":
    main()
