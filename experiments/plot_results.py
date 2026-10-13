#!/usr/bin/env python3
"""
Standalone graph generator for SIEVE Milestone 6.

Run this from the project root after running experiments/run_experiment.py:
    python3 experiments/plot_results.py

Requires matplotlib (optional, install in a local virtual environment):
    python3 -m venv .venv
    source .venv/bin/activate
    pip install matplotlib

Reads:   results/summary.csv
Writes:  results/figures/*.png
"""

import csv
import math
import pathlib
import statistics
import sys
from collections import defaultdict

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
RESULTS_DIR  = PROJECT_ROOT / "results"
FIGURES_DIR  = RESULTS_DIR / "figures"
SUMMARY_CSV  = RESULTS_DIR / "summary.csv"

POLICIES = ["FIFO", "LRU", "SIEVE"]
POLICY_COLORS  = {"FIFO": "#e07b39", "LRU": "#4c78a8", "SIEVE": "#72b355"}
POLICY_MARKERS = {"FIFO": "s",        "LRU": "^",        "SIEVE": "o"}


def load_summary():
    if not SUMMARY_CSV.exists():
        print(f"ERROR: {SUMMARY_CSV} not found. Run experiments/run_experiment.py first.")
        sys.exit(1)
    rows = list(csv.DictReader(open(SUMMARY_CSV, newline="")))
    # by_workload[wl][policy][cap] = (mean_hr, std_hr, mean_elapsed_ns, std_elapsed_ns)
    by_workload = defaultdict(lambda: defaultdict(dict))
    for row in rows:
        wl  = row["workload"]
        pol = row["policy"]
        cap = int(row["capacity"])
        by_workload[wl][pol][cap] = (
            float(row["mean_hit_ratio"]),
            float(row["std_hit_ratio"]),
            float(row["mean_elapsed_ns"]),
            float(row["std_elapsed_ns"]),
        )
    return by_workload


def plot_hit_ratio_vs_capacity(by_workload, ax_map):
    """Fill one subplot per workload: hit ratio vs capacity."""
    for wl, ax in ax_map.items():
        pol_data = by_workload.get(wl, {})
        for policy in POLICIES:
            data = pol_data.get(policy, {})
            if not data:
                continue
            caps  = sorted(data.keys())
            means = [data[c][0] for c in caps]
            stds  = [data[c][1] for c in caps]
            ax.errorbar(
                caps, means, yerr=stds,
                label=policy,
                color=POLICY_COLORS[policy],
                marker=POLICY_MARKERS[policy],
                linewidth=1.8, markersize=5, capsize=3,
            )
        ax.set_title(wl.replace("_", " ").title(), fontsize=11)
        ax.set_xlabel("Cache capacity (entries)", fontsize=9)
        ax.set_ylabel("Hit ratio", fontsize=9)
        ax.set_ylim(-0.05, 1.05)
        ax.set_xscale("log", base=2)
        ax.legend(fontsize=8)
        ax.grid(True, linestyle="--", alpha=0.4)


def make_graphs():
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("ERROR: matplotlib is not installed. Plotting requires matplotlib.")
        print("To install matplotlib in a project virtual environment, run:")
        print("    python3 -m venv .venv")
        print("    source .venv/bin/activate")
        print("    pip install matplotlib")
        print("Then rerun:")
        print("    make plot")
        print("  (or: python3 experiments/plot_results.py)")
        sys.exit(1)

    FIGURES_DIR.mkdir(parents=True, exist_ok=True)
    by_workload = load_summary()
    workloads = sorted(by_workload.keys())

    # ------------------------------------------------------------------ #
    # Figure 1: Hit ratio vs capacity                                      #
    # ------------------------------------------------------------------ #
    ncols = 3
    nrows = math.ceil(len(workloads) / ncols)
    fig, axes = plt.subplots(nrows, ncols, figsize=(5 * ncols, 4 * nrows))
    axes_flat = axes.flatten() if hasattr(axes, "flatten") else [axes]
    ax_map = {wl: axes_flat[i] for i, wl in enumerate(workloads)}

    plot_hit_ratio_vs_capacity(by_workload, ax_map)

    for idx in range(len(workloads), len(axes_flat)):
        axes_flat[idx].set_visible(False)

    fig.suptitle(
        "Hit Ratio vs Cache Capacity\n"
        "(error bars = ±1 std dev across seeds; FIFO=orange squares, LRU=blue triangles, SIEVE=green circles)",
        fontsize=12,
    )
    fig.tight_layout()
    out = FIGURES_DIR / "hit_ratio_vs_capacity.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"Saved: {out.relative_to(PROJECT_ROOT)}")

    # ------------------------------------------------------------------ #
    # Figure 2: Policy comparison bar chart at largest capacity            #
    # ------------------------------------------------------------------ #
    largest_cap = max(
        cap
        for wl_data in by_workload.values()
        for pol_data in wl_data.values()
        for cap in pol_data.keys()
    )

    fig, ax = plt.subplots(figsize=(10, 5))
    bar_width = 0.25
    x_positions = list(range(len(workloads)))

    for p_idx, policy in enumerate(POLICIES):
        means, stds = [], []
        for wl in workloads:
            data = by_workload[wl].get(policy, {})
            if largest_cap in data:
                means.append(data[largest_cap][0])
                stds.append(data[largest_cap][1])
            else:
                means.append(0.0)
                stds.append(0.0)
        offsets = [x + p_idx * bar_width for x in x_positions]
        ax.bar(offsets, means, bar_width, yerr=stds, capsize=4,
               label=policy, color=POLICY_COLORS[policy], alpha=0.85)

    ax.set_xticks([x + bar_width for x in x_positions])
    ax.set_xticklabels([w.replace("_", "\n") for w in workloads], fontsize=9)
    ax.set_ylabel("Hit ratio")
    ax.set_ylim(0, 1.05)
    ax.set_title(
        f"Policy Comparison at Cache Capacity = {largest_cap}\n"
        "(error bars = ±1 std dev across seeds)",
        fontsize=12,
    )
    ax.legend()
    ax.grid(True, axis="y", linestyle="--", alpha=0.4)
    fig.tight_layout()
    out = FIGURES_DIR / "policy_comparison.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"Saved: {out.relative_to(PROJECT_ROOT)}")

    # ------------------------------------------------------------------ #
    # Figure 3: Execution time                                             #
    # ------------------------------------------------------------------ #
    fig, ax = plt.subplots(figsize=(10, 5))
    for p_idx, policy in enumerate(POLICIES):
        means_ns = []
        for wl in workloads:
            data = by_workload[wl].get(policy, {})
            if data:
                means_ns.append(statistics.mean(v[2] for v in data.values()))
            else:
                means_ns.append(0.0)
        offsets = [x + p_idx * bar_width for x in x_positions]
        ax.bar(offsets, [ns / 1e6 for ns in means_ns], bar_width,
               label=policy, color=POLICY_COLORS[policy], alpha=0.85)

    ax.set_xticks([x + bar_width for x in x_positions])
    ax.set_xticklabels([w.replace("_", "\n") for w in workloads], fontsize=9)
    ax.set_ylabel("Mean elapsed time per run (ms)")
    ax.set_title(
        "Mean Execution Time per Workload and Policy\n"
        "(wall-clock from CLOCK_MONOTONIC; averaged over seeds and capacities;\n"
        "single-threaded; times are noisy — treat as approximate only)",
        fontsize=10,
    )
    ax.legend()
    ax.grid(True, axis="y", linestyle="--", alpha=0.4)
    fig.tight_layout()
    out = FIGURES_DIR / "execution_time.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"Saved: {out.relative_to(PROJECT_ROOT)}")

    print("\nAll graphs written to results/figures/")


if __name__ == "__main__":
    make_graphs()
