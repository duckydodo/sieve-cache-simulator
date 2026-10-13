# SIEVE Cache Simulator – Milestone 6 Experimental Analysis

## 1. Experimental Setup

### Simulator

This is a **user-space cache simulator** written in C11. It is not a Linux kernel
implementation. Each cache is a hash-table-backed doubly linked list. All three
policies (FIFO, LRU, SIEVE) use the same underlying data structure. The SIEVE
implementation follows **Algorithm 1** of the NSDI 2024 paper
*"SIEVE is Simpler than LRU: An Efficient Turn-Key Eviction Algorithm for Web Caches"*
(Zhang et al., 2024, <https://www.usenix.org/conference/nsdi24/presentation/zhang-yazhuo>).

### Reproduction Commands

```bash
# From the project root:
make clean && make all && make test     # verify all 51,498 assertions pass
make experiment                         # full experiment (or: python3 experiments/run_experiment.py)
make experiment-quick                   # reduced run for quick check
make plot                               # optional: generate plots (requires matplotlib)
```

### Workloads

`uniform`, `zipf`, `loop`, `scan`, `hot_scan`

| Workload | Description | Accesses | Key range |
|----------|-------------|----------|-----------|
| `uniform` | i.i.d. uniform random over key range | 20000 | 1000 |
| `zipf` | Exact discrete Zipf(α=1.0) distribution | 20000 | 1000 |
| `loop` | Cyclic sequential access over working set | 20000 | 200 |
| `scan` | Single sequential pass: key = access index | 5000 | = accesses |
| `hot_scan` | 50% hot-set accesses (size 100) + 50% sequential scan | 20000 | mixed |

### Seeds and Capacities

- **Seeds** for stochastic workloads (uniform, zipf, hot_scan): 42, 1337, 99991
- **Loop and scan** are deterministic; one run suffices.
- **Cache capacities tested**: 8, 16, 32, 64, 128, 256
- Each trace is generated once and replayed through FIFO, LRU, and SIEVE with
  a **fresh, empty cache** each time.

---

## 2. Hit-Ratio Results (Capacity = 256)

| Workload | FIFO hit% | LRU hit% | SIEVE hit% | Seeds |
|----------|-----------|----------|------------|-------|
| hot_scan   | 49.5% ±0.0 | 49.5% ±0.0 | 49.5% ±0.0 | 3 |
| loop       | 99.0%     | 99.0%    | 99.0%      | 1 |
| scan       | 0.0%      | 0.0%     | 0.0%       | 1 |
| uniform    | 25.5% ±0.8 | 25.6% ±1.0 | 25.4% ±0.2 | 3 |
| zipf       | 69.1% ±0.3 | 73.8% ±0.2 | 77.6% ±0.5 | 3 |

`hit% ±std` = mean hit-rate percentage ± 1 standard deviation across seeds.
Single-seed workloads (loop, scan) show no variability by design.

---

## 3. Interpretation

### 3.1 Where SIEVE outperforms LRU and FIFO

SIEVE's lazy-promotion mechanism gives it a clear advantage on **skewed workloads**
such as Zipf. When a small fraction of keys accounts for the majority of
accesses, SIEVE's `visited` bit allows it to protect frequently-seen items
without immediately moving them to the head of the queue on every hit — saving
pointer operations and preserving "good" items against eviction by the hand
scan. Across all tested capacities on the Zipf workload, SIEVE consistently
achieves a materially higher hit ratio than both FIFO and LRU (e.g., reaching
77.6% vs. 73.8% for LRU and 69.1% for FIFO at capacity 256, and 31.0% vs.
17.6% for LRU at capacity 8).

### 3.2 Where SIEVE does not consistently outperform

- **Uniform random**: all policies perform similarly because there is no
  locality for any policy to exploit. Hit ratios are roughly proportional to
  `capacity / key_range`.
- **Sequential scan**: all policies achieve 0% hit ratio (or very close to it)
  because every key is unique and the trace is longer than the cache. This is
  a known worst case for all cache eviction algorithms.
- **Looping**: the loop workload is either fully resident (all `key_range` keys
  fit in the cache → ~100% hit ratio) or cycles faster than the cache can warm
  up (capacity < key_range → structured misses). All three policies behave
  nearly identically because the access pattern is perfectly predictable and
  cyclic.
- **Hot-scan**: across all tested capacities (8 to 256), FIFO, LRU, and SIEVE
  achieve near-identical hit ratios (differing by at most 0.1 percentage point;
  all three reach exactly 49.50% at capacities 128 and 256). In this two-phase
  synthetic workload (a 50% hot phase over 100 keys followed by a 50% sequential
  scan over distinct keys), at capacity ≥ 100 the entire hot set fits in cache
  (yielding 9,900 hits / 20,000 accesses = 49.50%) while the scan phase yields
  0 hits regardless of policy. At capacity < 100, accesses during the hot phase
  are uniformly distributed across the 100 keys, yielding hit ratios roughly
  proportional to `(capacity / 100) * 0.5` across all three policies. The
  measured results establish that under this trace structure, SIEVE does not
  differentiate itself from FIFO or LRU.

### 3.3 Stability across seeds

Hit ratios across the stochastic workloads (**uniform**, **zipf**, and **hot_scan**)
are highly stable across the three seeds (standard deviations are below 1 percentage
point for all tested configurations). This confirms that 20,000 accesses is
sufficient for the estimates to converge.

---

## 4. Limitations

### 4.1 Synthetic workloads

All five workloads are synthetic. Real-world cache access traces (e.g. CDN logs,
Twitter/Facebook cache traces used in the NSDI paper) have more complex temporal
locality, bursty patterns, and key-popularity shifts that synthetic distributions
cannot fully replicate. **These results do not reproduce the paper's evaluation.**

### 4.2 Single-threaded user-space simulation

This simulator is single-threaded and does not measure memory bandwidth, TLB
pressure, or OS scheduling effects. Elapsed times are wall-clock nanoseconds
from `clock_gettime(CLOCK_MONOTONIC)` and are inherently noisy on a shared
machine. Time comparisons are illustrative only; do not interpret them as
CPU-cycle-accurate benchmarks.

### 4.3 Hash table and O(C) worst-case eviction

The hash table uses expected-O(1) lookup with MurmurHash3 finalization. SIEVE
eviction has **O(C) worst-case** complexity (the hand may scan the entire list
before finding an unvisited item). LRU and FIFO evict in O(1). For the trace
sizes used here (≤ 20,000 accesses, capacities ≤ 256), this difference is not
visible in timing results.

### 4.4 Why SIEVE is not universally superior

The NSDI 2024 paper shows SIEVE outperforming LRU on real-world CDN and key-value
cache traces. On synthetic uniform-random traces, SIEVE offers no advantage
because there is no temporal locality. On sequential scans, all algorithms
perform identically poorly. The right conclusion from this experiment is:
**SIEVE's advantage depends on skew in the access pattern**, not that SIEVE is
always better.

---

## 5. Output Files

| File | Description | Status |
|------|-------------|--------|
| `results/raw_results.csv` | All individual benchmark rows (one per policy/seed/capacity run) | Generated (198 rows) |
| `results/summary.csv` | Mean and std-dev hit ratios, grouped by workload/capacity/policy | Generated (90 rows) |
| `results/ANALYSIS.md` | Experimental analysis and interpretation | Generated |
| `traces/exp/` | Generated trace files (one per workload/seed combination) | Generated (11 traces) |
| `results/figures/*.png` | Visual comparison plots (requires matplotlib) | Generated |

*Note: Visual plots in `results/figures/` are optional and require `matplotlib`. When `matplotlib` is not installed, data collection, CSV generation, and analysis run without error. To generate plots, see README.md for instructions on setting up a virtual environment.*

---

*Generated by `experiments/run_experiment.py`. Do not edit manually.*
