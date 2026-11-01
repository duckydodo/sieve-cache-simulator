# SIEVE Cache Simulator

A high-performance, modular user-space cache simulator in C11 implementing the **FIFO**, **LRU**, and **SIEVE** cache eviction algorithms.

Based on the research paper:  
> **“SIEVE is Simpler than LRU: An Efficient Turn-Key Eviction Algorithm for Web Caches”**  
> Yazhuo Zhang, Paraschos Koutris, Rebecca Bian  
> *Proceedings of the 21st USENIX Symposium on Networked Systems Design and Implementation (NSDI '24)*, Santa Clara, CA, April 2024.  
> [Paper PDF](https://www.usenix.org/system/files/nsdi24-zhang-yazhuo.pdf) | [Official NSDI24-SIEVE Repository](https://github.com/cacheMon/NSDI24-SIEVE)

---

## Overview

Cache eviction algorithms dictate which items reside in bounded high-speed memory when capacity is exhausted. While **Least Recently Used (LRU)** has served as the de facto baseline for decades across operating systems, database buffer pools, and web content delivery networks (CDNs), LRU suffers from two systemic flaws:
1. **One-Hit Wonder Fragility:** In web caching workloads, 60%–80% of items are requested only once. LRU inserts every miss at the Most Recently Used (MRU) head, allowing cold, non-recurrent objects to flush valuable, frequently accessed items.
2. **Synchronization Contention on Hits:** Every cache hit in LRU requires unlinking and splicing the node to the head (6 pointer writes). In concurrent systems, every read hit becomes a structural write, creating a severe multi-core locking bottleneck.

**SIEVE** addresses both problems with a simple design:
- Objects are stored in a FIFO-like doubly linked list with a single `visited` boolean flag.
- **On Cache Hit:** An in-place bit update is performed (`node->visited = true`). **Zero pointer writes occur.**
- **On Eviction:** A moving `hand` scans backwards from the tail. If an item was visited, its bit is cleared to `false` and the hand advances (giving it a second chance). The first unvisited item is evicted. New items enter at the head with `visited = false`.
- **Lazy Demotion:** One-hit wonders are quickly discarded at the tail before ever being promoted, acting as a natural sieve for cold traffic.

---

## Implemented Algorithms

| Dimension | FIFO (First-In, First-Out) | LRU (Least Recently Used) | SIEVE (Zhang et al. 2024) |
| :--- | :--- | :--- | :--- |
| **Hit Operation** | No-op ($O(1)$) | Spliced to MRU Head ($O(1)$) | **In-place bit flip (`visited = 1`)** ($O(1)$) |
| **Pointer Writes on Hit** | **0** | **6 pointer updates** | **0 pointer updates** |
| **Eviction Candidate** | Always Tail node ($O(1)$) | Always Tail node ($O(1)$) | **Moving hand scan** (Amortized $O(1)$) |
| **Metadata per Object** | 2 list pointers | 2 list pointers | **1 bit (`visited`)** + 2 list pointers |
| **One-Hit Wonder Handling**| Poor | Extremely Vulnerable | **Quick tail discard (Lazy Demotion)** |
| **Worst-Case Eviction** | $O(1)$ | $O(1)$ | **$O(C)$** *(if all entries visited)* |
| **Bélády's Anomaly** | Susceptible | Immune (Stack Algorithm) | Susceptible (Non-stack algorithm) |

---

## Project Structure

```
SIEVE/
├── include/               # Public C headers
│   ├── cache.h            # Cache manager and policy interface
│   ├── benchmark.h        # Benchmark runner interface
│   ├── dlist.h            # Intrusive doubly linked list
│   ├── hashtable.h        # Fixed-bucket hash table (MurmurHash3 fmix64)
│   └── tracegen.h         # Synthetic trace generator
├── src/                   # Implementation files
│   ├── cache.c            # Cache engine (single memory owner)
│   ├── fifo.c             # FIFO eviction policy
│   ├── lru.c              # LRU eviction policy
│   ├── sieve.c            # SIEVE eviction policy (NSDI '24 Algorithm 1)
│   ├── dlist.c            # Intrusive doubly linked list primitives
│   ├── hashtable.c        # Power-of-two hash table (load factor <= 0.5)
│   ├── tracegen.c         # Workload generator library
│   └── benchmark.c        # Strict trace parser and benchmark runner
├── tests/                 # Unit and integration test suites
│   ├── test_ds.c          # Data structure tests (479 assertions)
│   ├── test_policies.c    # Cache policy tests (320 assertions)
│   ├── test_tracegen.c    # Trace generator tests (50,483 assertions)
│   ├── test_benchmark.c   # Benchmark and parser tests (313 assertions)
│   └── test_experiment_runner.py # Python experiment suite (9 tests)
├── tools/                 # Standalone CLI tools
│   └── tracegen.c         # Trace generator CLI executable
├── experiments/           # Automated experiment scripts
│   ├── config.json        # Machine-readable experiment parameters
│   ├── run_experiment.py  # Full experiment runner (traces -> CSV -> summary)
│   └── plot_results.py    # Publication plotting suite (generates PNG/PDF/SVG)
├── traces/exp/            # Pre-generated experimental traces (11 trace files)
├── results/               # Experiment outputs and empirical data
│   ├── raw_results.csv    # 198 benchmark rows across 66 invocations
│   ├── summary.csv        # Aggregated mean/std hit ratios and latency
│   ├── ANALYSIS.md        # Comprehensive empirical analysis
│   └── figures/           # 8 publication-quality figures (PNG, PDF, SVG)
├── Makefile               # Build configuration
├── REPORT.md              # Academic technical report
├── PRESENTATION.html      # Interactive standalone HTML5 presentation deck
├── PRESENTATION.md        # Marp-compatible markdown presentation slides
├── VIVA_PREPARATION.md    # Comprehensive Viva Voce defense guide
├── DEMO_SCRIPT.md         # Live terminal demonstration script
├── EVALUATION_CHECKLIST.md# Evaluation day checklist and timing budgets
└── README.md
```

---

## Building

### Prerequisites
- C11 compliant compiler (`gcc` or `clang`)
- POSIX-compliant operating system (Linux / macOS)
- Standard C library and `libm`
- Python 3.8+ (standard library only for benchmark automation; `matplotlib` optional for figures)

### Compilation
Build the benchmark runner, trace generator, and all test binaries:
```bash
make clean
make all
```

The build uses strict C11 flags: `-std=c11 -Wall -Wextra -Wpedantic`.

---

## Testing

Run all unit and integration test suites:
```bash
make test
```

This verifies **51,604 automated test assertions** across 5 test suites:
- **`bin/test_ds`:** 479 assertions (doubly linked list invariants, hash table collision chaining, power-of-two sizing).
- **`bin/test_policies`:** 320 assertions (FIFO order, LRU promotion, SIEVE hand traversal, all-visited wraparound, capacity-1 edge case).
- **`bin/test_tracegen`:** 50,483 assertions (Zipf distribution skew, uniform range bounds, looping modulus, scan ordering).
- **`bin/test_benchmark`:** 313 assertions (strict parser, trailing garbage rejection, negative integer rejection, CSV format compliance).
- **`test_experiment_runner.py`:** 9 tests (experiment matrix execution, metric aggregation, summary CSV calculation).

---

## Usage

### 1. Generating Traces (`bin/tracegen`)

Generate synthetic cache access traces:
```bash
# Uniform random workload (zero temporal locality)
./bin/tracegen -w uniform -n 20000 -k 1000 -s 42 -o traces/uniform.txt

# Zipfian popularity skew (power-law web/CDN distribution)
./bin/tracegen -w zipf -n 20000 -k 1000 -a 1.0 -s 42 -o traces/zipf.txt

# Cyclic loop workload (tests working-set capacity cliff)
./bin/tracegen -w loop -n 20000 -k 200 -o traces/loop.txt

# Sequential scan (models streaming I/O pollution)
./bin/tracegen -w scan -n 5000 -o traces/scan.txt

# Hot-set plus scan (interleaved hot working set + streaming scan)
./bin/tracegen -w hot_scan -n 20000 -k 1000 --hot-size 100 --hot-frac 0.5 -s 42 -o traces/hot_scan.txt
```

#### CLI Options
- `-w, --workload <type>`: Workload type (`uniform`, `zipf`, `loop`, `scan`, `hot_scan`)
- `-n, --accesses <count>`: Total request count (default: 10,000)
- `-k, --keys <count>`: Key universe size (default: 1,000)
- `-s, --seed <number>`: PRNG seed for reproducibility
- `-a, --alpha <float>`: Zipfian skew parameter (default: 1.0)
- `--hot-size <count>`: Hot working set size for `hot_scan` (default: 50)
- `--hot-frac <float>`: Fraction of accesses targeting the hot set (default: 0.5)
- `-o, --output <file>`: Output path (default: stdout)

---

### 2. Running Benchmarks (`bin/benchmark`)

Replay an identical trace sequence through FIFO, LRU, and SIEVE:
```bash
# Benchmark single capacity:
./bin/benchmark -t traces/exp/zipf_42.txt -c 64 -o results/zipf_64.csv

# Benchmark multiple capacities in one invocation:
./bin/benchmark -t traces/exp/zipf_42.txt -c 8,16,32,64,128,256 -w zipf -s 42
```

#### Timing Isolation
Timing is measured using POSIX `clock_gettime(CLOCK_MONOTONIC)` wrapped strictly around the sequential cache access loop. Trace reading, memory allocation, string parsing, and CSV printing are **strictly excluded** from timing metrics.

#### Output Format (RFC 4180 CSV)
The benchmark outputs 13 standardized columns:
`workload, trace, seed, accesses, distinct_keys, capacity, policy, hits, misses, evictions, hit_ratio, miss_ratio, elapsed_ns`.

---

## Experiments & Reproducibility

The repository includes an end-to-end experimental framework configured in `experiments/config.json`:
- **5 Workload Types:** `zipf` ($\alpha=1.0$), `uniform`, `loop`, `scan`, `hot_scan`
- **6 Cache Capacities:** $C \in \{8, 16, 32, 64, 128, 256\}$ (0.8% to 25.6% working set coverage)
- **Seeds:** 3 independent seeds (`42`, `1337`, `99991`) for stochastic workloads; deterministic replay for static traces
- **Total Executions:** 66 benchmark invocations producing 198 result rows

### Running the Full Matrix
```bash
make experiment
# or: python3 experiments/run_experiment.py
```

### Quick Verification Run
```bash
make experiment-quick
# or: python3 experiments/run_experiment.py --quick
```

### Generating Visual Plots
Generating publication figures requires `matplotlib`:
```bash
make plot
# or: python3 experiments/plot_results.py
```
This produces 8 figures in `results/figures/` (saved as PNG, vector PDF, and vector SVG).

---

## Key Experimental Results

All results below represent actual empirical data measured across identical trace sequences (`results/summary.csv`):

### 1. Popularity Skew (`zipf`, $\alpha = 1.0, K=1,000, N=20,000$)

| Capacity ($C$) | Coverage ($C/K$) | FIFO Hit % | LRU Hit % | SIEVE Hit % | **Absolute Gain vs LRU** | **Miss Ratio Reduction** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **8** | 0.8% | 15.44% | 17.60% | **30.96%** | <span style="color:green;font-weight:bold;">+13.36%</span> | **+16.21%** |
| **16** | 1.6% | 23.56% | 27.77% | **41.09%** | <span style="color:green;font-weight:bold;">+13.32%</span> | **+18.44%** |
| **32** | 3.2% | 33.52% | 38.83% | **50.37%** | <span style="color:green;font-weight:bold;">+11.54%</span> | **+18.86%** |
| **64** | 6.4% | 44.44% | 50.10% | **60.08%** | <span style="color:green;font-weight:bold;">+9.98%</span> | **+20.00%** |
| **128** | 12.8% | 56.31% | 61.53% | **68.83%** | <span style="color:green;font-weight:bold;">+7.30%</span> | **+18.98%** |
| **256** | 25.6% | 69.12% | 73.80% | **77.58%** | <span style="color:green;font-weight:bold;">+3.78%</span> | **+14.43%** |

- **Peak Advantage:** SIEVE's advantage peaks under constrained capacity ($+13.36\%$ at $C=8$), where one-hit wonders severely degrade LRU.
- **Miss Elimination:** SIEVE eliminates **14.4% to 20.0% of all cache misses** incurred by LRU.
- **Storage Write Savings:** By suppressing misses, SIEVE saves **2,671 evictions** at $C=8$ and **1,997 evictions** at $C=64$, directly protecting flash SSD endurance.

### 2. Single-Threaded Hit Latency (Resident Loop, $C=256 > K=200$)
Under fully resident accesses (**99.0% hit ratio**), eliminating pointer writes delivers a substantial hardware efficiency advantage:
- **LRU Hit Path:** Splicing node to MRU head (6 pointer writes) $\rightarrow$ **61.03 ns/access** (16.39 MOPS).
- **SIEVE Hit Path:** In-place boolean bit update (`node->visited = true`) $\rightarrow$ **26.09 ns/access** (38.33 MOPS).
- **Speedup:** SIEVE is **2.34× faster than LRU on resident cache hits**.

### 3. Working-Set Transitions & Baselines
- **The Loop Cliff (`loop`, $K=200$):** Yields **0.00% hits** for $C \le 128 < 200$ (thrashing) and jumps cleanly to **99.00% hits** at $C=256 > 200$ across all policies.
- **Hot-Scan Ceiling (`hot_scan`, $H=100$):** Scales linearly and hits an exact theoretical ceiling of **49.50%** at $C \ge 128$ for all policies.
- **Uniform Random (`uniform`, $K=1,000$):** Exactly matches theoretical random selection probability $P(\text{Hit}) = C/K$ across all policies (e.g., 6.45% observed vs. 6.40% theoretical at $C=64$).

---

## Limitations & Threats to Validity

1. **Synthetic vs. Production Traces:** Synthetic Zipf assumes a stationary distribution with fixed popularity ranks. Real-world CDN traffic exhibits non-stationary popularity drift, breaking news spikes, and diurnal patterns.
2. **Homogeneous Keys:** The simulator operates on fixed 64-bit keys. Real web caches manage variable-size payloads (100B to 50MB) and HTTP TTL expirations.
3. **Single-Threaded Model:** The simulator is single-threaded to isolate algorithmic complexity. It does not directly evaluate multi-threaded lock contention (where the original paper observed up to 7.8× throughput scaling due to read locks on hits).
4. **Hit-Dependent Speedup:** SIEVE's runtime speedup is strictly observed on **hit-dominated workloads**. On miss-heavy workloads (`scan`, `uniform`), eviction hand traversal and cache allocation costs dominate, resulting in latency comparable to FIFO and LRU.

---

## Academic Citation

If you use this simulator or reference its empirical findings, please cite the original research paper:

```bibtex
@inproceedings{zhang2024sieve,
  author    = {Yazhuo Zhang and Paraschos Koutris and Rebecca Bian},
  title     = {{SIEVE} is Simpler than {LRU}: An Efficient Turn-Key Eviction Algorithm for Web Caches},
  booktitle = {21st USENIX Symposium on Networked Systems Design and Implementation (NSDI 24)},
  year      = {2024},
  pages     = {409--424},
  address   = {Santa Clara, CA},
  url       = {https://www.usenix.org/conference/nsdi24/presentation/zhang-yazhuo},
  publisher = {USENIX Association}
}
```

---

## License

This project is developed for undergraduate Operating Systems education and research reproduction. The SIEVE algorithm is patented/published by Zhang et al. (USENIX NSDI 2024). Source code is released under the MIT License.
