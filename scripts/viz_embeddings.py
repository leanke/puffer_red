#!/usr/bin/env python3
"""Visualize policy embeddings from captured data.

Reads captures.jsonl produced by the PokemonRed policy (capture_viz=True)
and generates PCA projections, value/action time series, and embedding
trajectory plots.

Usage:
    python scripts/viz_embeddings.py viz_data/captures.jsonl
    python scripts/viz_embeddings.py viz_data/captures.jsonl --output embeddings.png
    python scripts/viz_embeddings.py viz_data/captures.jsonl --embedding cnn_flat
    python scripts/viz_embeddings.py viz_data/captures.jsonl --tsne --show
"""

import argparse
import json
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


def pca(data, n_components=2):
    """Simple PCA using numpy SVD."""
    data = np.array(data, dtype=np.float32)
    mean = data.mean(axis=0)
    centered = data - mean
    U, S, Vt = np.linalg.svd(centered, full_matrices=False)
    projected = centered @ Vt[:n_components].T
    explained_var = (S[:n_components] ** 2) / (S ** 2).sum()
    return projected, explained_var


def try_tsne(data, n_components=2, perplexity=30):
    """Try t-SNE via sklearn, fall back to PCA."""
    try:
        from sklearn.manifold import TSNE
        tsne = TSNE(n_components=n_components, perplexity=min(perplexity, len(data) - 1),
                     random_state=42)
        return tsne.fit_transform(np.array(data, dtype=np.float32)), None
    except ImportError:
        print("sklearn not available, falling back to PCA", file=sys.stderr)
        return pca(data, n_components)


def load_captures(path):
    """Load all captures from JSONL file."""
    captures = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            captures.append(json.loads(line))
    return captures


EMBEDDING_KEYS = [
    "pre_lstm_hidden",
    "post_lstm_hidden",
    "cnn_flat",
    "coord_emb",
    "ram_cat",
]


def plot_embedding_space(captures, embedding_key="pre_lstm_hidden",
                         use_tsne=False, output=None, show=False):
    """2D projection of embedding vectors colored by value estimate."""
    plt = get_plt(interactive=show)

    embeddings = []
    values = []
    steps = []
    for cap in captures:
        emb = cap.get("embeddings", {}).get(embedding_key)
        val = cap.get("embeddings", {}).get("value")
        if emb is None:
            continue
        embeddings.append(emb)
        values.append(val if val is not None else 0.0)
        steps.append(cap["step"])

    if len(embeddings) < 3:
        print(f"Need at least 3 captures, got {len(embeddings)}", file=sys.stderr)
        sys.exit(1)

    embeddings = np.array(embeddings, dtype=np.float32)
    values = np.array(values)
    steps = np.array(steps)

    method_name = "t-SNE" if use_tsne else "PCA"
    if use_tsne:
        projected, explained = try_tsne(embeddings)
    else:
        projected, explained = pca(embeddings)

    fig, axes = plt.subplots(1, 2, figsize=(16, 7))
    fig.suptitle(f"{embedding_key} — {method_name} Projection "
                 f"({len(captures)} captures)", fontsize=14)

    # Color by value estimate
    ax = axes[0]
    sc = ax.scatter(projected[:, 0], projected[:, 1], c=values,
                    cmap="RdYlGn", s=15, alpha=0.7, edgecolors="none")
    ax.set_xlabel(f"{method_name} 1" + (f" ({explained[0]:.1%})" if explained is not None else ""))
    ax.set_ylabel(f"{method_name} 2" + (f" ({explained[1]:.1%})" if explained is not None else ""))
    ax.set_title("Colored by Value Estimate")
    plt.colorbar(sc, ax=ax, label="Value")

    # Color by training step (trajectory)
    ax = axes[1]
    sc = ax.scatter(projected[:, 0], projected[:, 1], c=steps,
                    cmap="viridis", s=15, alpha=0.7, edgecolors="none")
    # Draw trajectory lines
    ax.plot(projected[:, 0], projected[:, 1], color="gray",
            linewidth=0.3, alpha=0.4, zorder=0)
    ax.set_xlabel(f"{method_name} 1")
    ax.set_ylabel(f"{method_name} 2")
    ax.set_title("Colored by Step (trajectory)")
    plt.colorbar(sc, ax=ax, label="Step")

    plt.tight_layout()
    out = output or f"embeddings_{embedding_key}.png"
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved embedding projection to {out}")
    if show:
        plt.show()
    plt.close(fig)


def plot_time_series(captures, output=None, show=False):
    """Plot value estimates and action distributions over time."""
    plt = get_plt(interactive=show)

    steps = []
    values = []
    logits_list = []
    for cap in captures:
        emb = cap.get("embeddings", {})
        val = emb.get("value")
        logits = emb.get("actor_logits")
        if val is None or logits is None:
            continue
        steps.append(cap["step"])
        values.append(val)
        logits_list.append(logits)

    if not steps:
        print("No captures with value/logits data found", file=sys.stderr)
        return

    steps = np.array(steps)
    values = np.array(values)
    logits_arr = np.array(logits_list)

    # Softmax to get action probabilities
    logits_shifted = logits_arr - logits_arr.max(axis=1, keepdims=True)
    exp_logits = np.exp(logits_shifted)
    action_probs = exp_logits / exp_logits.sum(axis=1, keepdims=True)

    action_labels = [
        "None", "Up", "Down", "Left", "Right", "A", "B", "Start", "Select"
    ]

    fig, axes = plt.subplots(3, 1, figsize=(14, 10),
                             gridspec_kw={"height_ratios": [2, 3, 2]})
    fig.suptitle("Policy Output Over Time", fontsize=14)

    # Value estimate
    ax = axes[0]
    ax.plot(steps, values, linewidth=0.8, color="steelblue")
    ax.set_ylabel("Value Estimate")
    ax.set_title("Value Function")
    ax.grid(True, alpha=0.3)

    # Action probabilities stacked area
    ax = axes[1]
    ax.stackplot(steps, action_probs.T,
                 labels=action_labels[:action_probs.shape[1]],
                 alpha=0.8)
    ax.set_ylabel("Probability")
    ax.set_title("Action Distribution")
    ax.legend(loc="upper right", fontsize=7, ncol=3)
    ax.set_ylim(0, 1)
    ax.grid(True, alpha=0.3)

    # Entropy of action distribution
    ax = axes[2]
    entropy = -np.sum(action_probs * np.log(action_probs + 1e-8), axis=1)
    ax.plot(steps, entropy, linewidth=0.8, color="darkorange")
    ax.set_ylabel("Entropy (nats)")
    ax.set_xlabel("Step")
    ax.set_title("Policy Entropy")
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = output or "time_series.png"
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved time series to {out}")
    if show:
        plt.show()
    plt.close(fig)


def plot_ram_trajectory(captures, output=None, show=False):
    """Plot agent position trajectory colored by value estimate."""
    plt = get_plt(interactive=show)

    xs, ys, maps, values, steps = [], [], [], [], []
    for cap in captures:
        ram = cap.get("ram_raw")
        val = cap.get("embeddings", {}).get("value")
        if ram is None or val is None:
            continue
        xs.append(ram[0])
        ys.append(ram[1])
        maps.append(ram[2])
        values.append(val)
        steps.append(cap["step"])

    if not xs:
        print("No RAM data found", file=sys.stderr)
        return

    xs, ys = np.array(xs), np.array(ys)
    values = np.array(values)

    fig, axes = plt.subplots(1, 2, figsize=(14, 6))
    fig.suptitle("Agent Position Trajectory", fontsize=14)

    ax = axes[0]
    sc = ax.scatter(xs, ys, c=values, cmap="RdYlGn", s=10, alpha=0.6)
    ax.set_xlabel("X Position")
    ax.set_ylabel("Y Position")
    ax.set_title("Colored by Value Estimate")
    ax.invert_yaxis()
    plt.colorbar(sc, ax=ax, label="Value")

    ax = axes[1]
    sc = ax.scatter(xs, ys, c=steps, cmap="viridis", s=10, alpha=0.6)
    ax.plot(xs, ys, color="gray", linewidth=0.2, alpha=0.3, zorder=0)
    ax.set_xlabel("X Position")
    ax.set_ylabel("Y Position")
    ax.set_title("Colored by Step")
    ax.invert_yaxis()
    plt.colorbar(sc, ax=ax, label="Step")

    plt.tight_layout()
    out = output or "ram_trajectory.png"
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved RAM trajectory to {out}")
    if show:
        plt.show()
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(
        description="Visualize policy embeddings from captures.")
    parser.add_argument("captures_file", help="Path to captures.jsonl")
    parser.add_argument("--embedding", default="pre_lstm_hidden",
                        choices=EMBEDDING_KEYS,
                        help="Which embedding to project (default: pre_lstm_hidden)")
    parser.add_argument("--tsne", action="store_true",
                        help="Use t-SNE instead of PCA (requires sklearn)")
    parser.add_argument("--output", "-o", default=None,
                        help="Output image path")
    parser.add_argument("--show", action="store_true",
                        help="Show interactive plot window")
    parser.add_argument("--time-series", action="store_true",
                        help="Plot value/action time series instead of embeddings")
    parser.add_argument("--trajectory", action="store_true",
                        help="Plot agent position trajectory from RAM data")
    args = parser.parse_args()

    captures = load_captures(args.captures_file)
    if not captures:
        print(f"No captures found in {args.captures_file}", file=sys.stderr)
        sys.exit(1)

    print(f"Loaded {len(captures)} captures "
          f"(steps {captures[0]['step']}–{captures[-1]['step']})")

    if args.time_series:
        plot_time_series(captures, output=args.output, show=args.show)
    elif args.trajectory:
        plot_ram_trajectory(captures, output=args.output, show=args.show)
    else:
        plot_embedding_space(captures, embedding_key=args.embedding,
                             use_tsne=args.tsne, output=args.output,
                             show=args.show)


if __name__ == "__main__":
    main()
