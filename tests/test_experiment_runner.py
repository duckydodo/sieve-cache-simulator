#!/usr/bin/env python3
"""
Regression and validation tests for Milestone 6 experiment scripts:
- experiments/run_experiment.py
- experiments/plot_results.py

Tests cover:
1. Trace fingerprinting and stale trace detection (switching quick -> full regenerates)
2. Access count validation (validate_row rejects rows with mismatched access count)
3. --analysis-only context derivation for both quick and full summary files
4. Relative/portable trace paths in raw_results.csv
5. Graceful loading of summary metrics (including ns_per_access) in plot_results.py
"""

import csv
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

PROJECT_ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT / "experiments"))

import run_experiment
import plot_results


class TestExperimentRunner(unittest.TestCase):
    def setUp(self):
        self.test_dir = pathlib.Path(tempfile.mkdtemp(prefix="sieve_test_exp_"))

    def tearDown(self):
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def test_stale_trace_fingerprint_mismatch(self):
        """Test that switching parameters detects a fingerprint mismatch and regenerates."""
        trace_file = self.test_dir / "uniform_42.txt"
        quick_args = ["-w", "uniform", "-n", "5000", "-k", "500"]
        full_args  = ["-w", "uniform", "-n", "20000", "-k", "1000"]
        seed = 42

        # 1. Simulate quick trace generation
        trace_file.write_text("10\n20\n30\n")
        quick_fp = run_experiment._fingerprint_key(quick_args, seed)
        run_experiment._write_fingerprint(trace_file, quick_fp)

        # Quick trace is valid for quick_args
        self.assertTrue(run_experiment._trace_is_valid(trace_file, quick_args, seed))

        # But it must be flagged invalid for full_args
        self.assertFalse(run_experiment._trace_is_valid(trace_file, full_args, seed))

        # If meta file is deleted/missing, it must also be flagged invalid
        meta_file = run_experiment._meta_path(trace_file)
        meta_file.unlink()
        self.assertFalse(run_experiment._trace_is_valid(trace_file, quick_args, seed))

    def test_validate_row_access_count_check(self):
        """Test that validate_row catches unexpected access count."""
        valid_row = {
            "accesses": "20000",
            "hits": "5000",
            "misses": "15000",
            "evictions": "14900",
            "hit_ratio": "0.250000",
            "miss_ratio": "0.750000",
        }
        # Correct expected count -> no errors
        self.assertEqual(run_experiment.validate_row(valid_row, expected_accesses=20000), [])

        # Stale trace count (e.g. 5000 instead of 20000) -> error
        errors = run_experiment.validate_row(valid_row, expected_accesses=5000)
        self.assertTrue(any("!= expected" in e for e in errors))

    def test_validate_row_math_invariants(self):
        """Test that validate_row catches arithmetic violations."""
        bad_sum = {
            "accesses": "100",
            "hits": "60",
            "misses": "30",  # 60 + 30 != 100
            "evictions": "20",
            "hit_ratio": "0.600000",
            "miss_ratio": "0.300000",
        }
        errors = run_experiment.validate_row(bad_sum)
        self.assertTrue(any("!=" in e for e in errors))

        bad_evictions = {
            "accesses": "100",
            "hits": "50",
            "misses": "50",
            "evictions": "60",  # evictions > misses
            "hit_ratio": "0.500000",
            "miss_ratio": "0.500000",
        }
        errors = run_experiment.validate_row(bad_evictions)
        self.assertTrue(any("evictions" in e for e in errors))

    def test_analysis_only_derives_context_correctly(self):
        """Test that context is derived accurately from summary CSV rows."""
        summary_rows = [
            {
                "workload": "zipf",
                "capacity": "32",
                "policy": "FIFO",
                "seed_count": "3",
                "mean_hit_ratio": "0.450000",
                "std_hit_ratio": "0.001000",
                "mean_miss_ratio": "0.550000",
                "std_miss_ratio": "0.001000",
                "mean_hit_ratio_pct": "45.00",
                "mean_elapsed_ns": "1200000",
                "std_elapsed_ns": "5000",
                "mean_ns_per_access": "60.00",
                "std_ns_per_access": "0.25",
                "total_accesses": "60000",
            },
            {
                "workload": "zipf",
                "capacity": "64",
                "policy": "FIFO",
                "seed_count": "3",
                "mean_hit_ratio": "0.550000",
                "std_hit_ratio": "0.001000",
                "mean_miss_ratio": "0.550000",
                "std_miss_ratio": "0.001000",
                "mean_hit_ratio_pct": "55.00",
                "mean_elapsed_ns": "1200000",
                "std_elapsed_ns": "5000",
                "mean_ns_per_access": "60.00",
                "std_ns_per_access": "0.25",
                "total_accesses": "60000",
            },
        ]
        wl_names, capacities, accesses = run_experiment._derive_context_from_summary(summary_rows)
        self.assertEqual(wl_names, ["zipf"])
        self.assertEqual(capacities, [32, 64])
        self.assertEqual(accesses["zipf"], 20000)

    def test_analysis_only_command_does_not_invoke_subprocesses(self):
        """Verify that running with --analysis-only does not invoke tracegen or benchmark."""
        # Run --analysis-only in a subprocess with mocked PATH to verify no compilation/benchmarks run
        cmd = [sys.executable, str(PROJECT_ROOT / "experiments" / "run_experiment.py"), "--analysis-only"]
        res = subprocess.run(cmd, cwd=PROJECT_ROOT, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("--analysis-only: reading context from results/summary.csv", res.stdout)
        self.assertNotIn("Phase 2: Generating traces", res.stdout)
        self.assertNotIn("Phase 3: Running benchmarks", res.stdout)

    def test_raw_results_paths_are_relative(self):
        """Verify that existing results/raw_results.csv contains only repo-relative paths."""
        raw_csv_path = PROJECT_ROOT / "results" / "raw_results.csv"
        self.assertTrue(raw_csv_path.exists())
        with open(raw_csv_path, newline="") as f:
            reader = csv.DictReader(f)
            for row in reader:
                trace = row["trace"]
                self.assertFalse(trace.startswith("/"), f"Absolute path found: {trace}")
                self.assertTrue(trace.startswith("traces/"), f"Unexpected trace path: {trace}")

    def test_plot_results_load_summary(self):
        """Verify plot_results load_summary successfully loads ns_per_access."""
        by_workload = plot_results.load_summary()
        self.assertIn("uniform", by_workload)
        # Check that the 5th element is positive ns_per_access
        fifo_cap8 = by_workload["uniform"]["FIFO"][8]
        self.assertEqual(len(fifo_cap8), 5)
        ns_per_access = fifo_cap8[4]
        self.assertGreater(ns_per_access, 0.0)

    def test_write_config_json(self):
        """Verify write_config_json writes a valid JSON file with required fields."""
        config_path = run_experiment.write_config_json(
            run_experiment.WORKLOAD_DEFS_FULL,
            run_experiment.CAPACITIES_FULL,
            mode="full"
        )
        self.assertTrue(config_path.exists())
        with open(config_path) as f:
            data = json.load(f)
        self.assertEqual(data["experiment_configuration"]["mode"], "full")
        self.assertEqual(len(data["experiment_configuration"]["workloads"]), 5)
        self.assertEqual(data["experiment_configuration"]["capacities"], [8, 16, 32, 64, 128, 256])
        self.assertEqual(data["experiment_configuration"]["planned_benchmark_invocations"], 66)
        self.assertEqual(data["experiment_configuration"]["expected_result_rows"], 198)

    def test_figures_exist_and_non_empty(self):
        """Verify that all publication figures exist in PNG, PDF, and SVG and have positive size."""
        fig_dir = PROJECT_ROOT / "results" / "figures"
        expected_bases = [
            "fig1_hit_ratio_vs_capacity",
            "fig2_miss_ratio_comparison",
            "fig3_evictions_vs_capacity",
            "fig4_runtime_throughput",
            "fig5_zipf_detailed_analysis",
            "fig6_working_set_transitions",
            "fig7_uniform_and_scan_baselines",
            "fig8_summary_dashboard",
        ]
        for base in expected_bases:
            for ext in [".png", ".pdf", ".svg"]:
                p = fig_dir / f"{base}{ext}"
                self.assertTrue(p.exists(), f"Figure file missing: {p}")
                self.assertGreater(p.stat().st_size, 1000, f"Figure file suspiciously small: {p}")


if __name__ == "__main__":
    unittest.main()


