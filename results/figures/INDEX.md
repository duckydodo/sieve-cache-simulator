# Visualizations Index: SIEVE Cache Eviction Algorithm Evaluation

This directory contains publication-quality visualizations generated from actual, validated benchmark runs comparing **FIFO**, **LRU**, and **SIEVE** cache eviction policies across five representative workloads.

All figures are provided in three standardized formats:
- **PNG**: 300 DPI high-resolution raster image suitable for slide decks and reports.
- **PDF**: Scalable vector graphics suitable for publication in LaTeX manuscripts.
- **SVG**: Scalable vector graphics suitable for interactive web rendering.

---

## Figure Catalog

### Figure 1: Hit Ratio vs. Cache Capacity Across Workloads
- **Files**: [`fig1_hit_ratio_vs_capacity.png`](file:///home/chitta/Projects/SIEVE/results/figures/fig1_hit_ratio_vs_capacity.png), [`.pdf`](file:///home/chitta/Projects/SIEVE/results/figures/fig1_hit_ratio_vs_capacity.pdf), [`.svg`](file:///home/chitta/Projects/SIEVE/results/figures/fig1_hit_ratio_vs_capacity.svg)
- **Format**: 2×3 multi-panel grid covering all five synthetic workloads plus an architectural scorecard.
- **Description**: Displays cache hit ratio (0% to 100%) as a function of cache capacity ($C \in \{8, 16, 32, 64, 128, 256\}$ on a $\log_2$ scale). Points indicate mean hit ratio across seeds, with error bars representing $\pm 1$ standard deviation.
- **Key Empirical Observations**:
  1. **`zipf`**: SIEVE consistently outperforms both FIFO and LRU across every capacity tested (+3.78% to +13.36% absolute hit ratio increase over LRU).
  2. **`loop`**: Sharp step-function phase transition. Capacities $C \le 128 < K=200$ suffer complete thrashing (0.00% hits), while $C=256 > K$ achieves 99.00% hits.
  3. **`hot_scan`**: Performance scales linearly with capacity up to $C=64$ (31.88%), then saturates at the 49.50% theoretical ceiling once the hot set ($H=100$) fits in cache ($C \ge 128$).
  4. **`scan`**: All policies exhibit complete vulnerability to open-ended sequential scans (0.00% hits).
  5. **`uniform`**: All policies closely track the theoretical expectation $E[\text{Hit Ratio}] = C/K = C/1000$.

---

### Figure 2: Miss Ratio Progression and Miss Ratio Reduction (MRR)
- **Files**: [`fig2_miss_ratio_comparison.png`](file:///home/chitta/Projects/SIEVE/results/figures/fig2_miss_ratio_comparison.png), [`.pdf`](file:///home/chitta/Projects/SIEVE/results/figures/fig2_miss_ratio_comparison.pdf), [`.svg`](file:///home/chitta/Projects/SIEVE/results/figures/fig2_miss_ratio_comparison.svg)
- **Format**: Dual-panel comparison (Panel A: Miss ratio curves on Zipf; Panel B: Percentage of misses eliminated).
- **Description**: Panel A compares the miss ratio curves on the skewed Zipf workload. Panel B presents the **Miss Ratio Reduction (MRR)** achieved by SIEVE:
  $$\text{MRR} = \frac{\text{Miss Ratio}(\text{Baseline}) - \text{Miss Ratio}(\text{SIEVE})}{\text{Miss Ratio}(\text{Baseline})} \times 100\%$$
- **Key Empirical Observations**:
  - Over LRU: SIEVE eliminates **14.4% to 20.0%** of cache misses across all capacities. The reduction peaks at $C=64$ where SIEVE eliminates **20.00% of LRU misses** (reducing miss ratio from 49.90% to 39.92%).
  - Over FIFO: SIEVE eliminates **26.9% to 29.5%** of cache misses at intermediate capacities ($C=16$ to $C=64$).
  - *Practical Implication*: In high-scale web caches and CDNs, a 20% reduction in miss traffic dramatically reduces backend database load and network transit costs.

---

### Figure 3: Eviction Counts vs. Capacity & Relation to Misses
- **Files**: [`fig3_evictions_vs_capacity.png`](file:///home/chitta/Projects/SIEVE/results/figures/fig3_evictions_vs_capacity.png), [`.pdf`](file:///home/chitta/Projects/SIEVE/results/figures/fig3_evictions_vs_capacity.pdf), [`.svg`](file:///home/chitta/Projects/SIEVE/results/figures/fig3_evictions_vs_capacity.svg)
- **Format**: Dual-panel comparison (Panel A: Total evictions; Panel B: Evictions saved by SIEVE).
- **Description**: Illustrates total eviction counts across capacities on `zipf` ($N=20,000, K=1,000$) and confirms the caching invariant $\text{evictions} = \text{misses} - C$ once the cache is filled.
- **Key Empirical Observations**:
  - Because $\text{evictions} = N - \text{hits} - C$, policies that produce higher hit counts perform substantially fewer evictions.
  - At $C=64$, SIEVE performs **1,996 fewer evictions than LRU** and **3,128 fewer evictions than FIFO** over a 20,000-access window.
  - *Practical Implication*: Fewer evictions directly reduce SSD flash wear-out and write amplification in disk-backed cache storage engines.

---

### Figure 4: Runtime and Replay Throughput Comparison
- **Files**: [`fig4_runtime_throughput.png`](file:///home/chitta/Projects/SIEVE/results/figures/fig4_runtime_throughput.png), [`.pdf`](file:///home/chitta/Projects/SIEVE/results/figures/fig4_runtime_throughput.pdf), [`.svg`](file:///home/chitta/Projects/SIEVE/results/figures/fig4_runtime_throughput.svg)
- **Format**: Dual-panel bar chart (Panel A: Latency in ns/access; Panel B: Throughput in MOPS).
- **Description**: Evaluates execution latency and throughput measured strictly around the sequential cache access loop using POSIX `clock_gettime(CLOCK_MONOTONIC)`. Excludes trace I/O, memory allocations, and CSV formatting.
- **Key Empirical Observations**:
  - Single-threaded user-space throughput remains consistently high across all three policies, ranging from **12 to 22 Million Operations Per Second (MOPS)** (~45 to 85 ns per access).
  - Variations in single-threaded replay time are primarily dominated by hash table lookups and whether an access is a hit or miss.
  - *Concurrency Context*: While single-threaded CPU times are comparable, the NSDI '24 paper demonstrates that SIEVE's true computational advantage emerges under multi-core concurrency: SIEVE requires no queue mutations or write locks on cache hits, enabling lock-free scaling that LRU cannot achieve.

---

### Figure 5: Deep-Dive Analysis of Skewed Workloads (Zipf)
- **Files**: [`fig5_zipf_detailed_analysis.png`](file:///home/chitta/Projects/SIEVE/results/figures/fig5_zipf_detailed_analysis.png), [`.pdf`](file:///home/chitta/Projects/SIEVE/results/figures/fig5_zipf_detailed_analysis.pdf), [`.svg`](file:///home/chitta/Projects/SIEVE/results/figures/fig5_zipf_detailed_analysis.svg)
- **Format**: Dual-panel analysis (Panel A: Hit ratio curves; Panel B: Absolute percentage point gain of SIEVE).
- **Description**: Focuses on the defining workload of web caching: discrete Zipfian popularity skew ($\alpha=1.0$).
- **Key Empirical Observations**:
  - Panel B highlights that SIEVE's absolute margin over LRU is largest at low-to-medium cache sizes:
    - At $C=8$ (0.8% working set): **+13.36%** over LRU (30.96% vs 17.60%).
    - At $C=16$ (1.6% working set): **+13.32%** over LRU (41.09% vs 27.77%).
    - At $C=32$ (3.2% working set): **+11.54%** over LRU (50.37% vs 38.83%).
    - At $C=64$ (6.4% working set): **+9.98%** over LRU (60.08% vs 50.10%).
  - *Mechanism*: In skewed distributions, rare "one-hit wonders" enter the cache once and are never re-accessed. SIEVE inserts them at the head with `visited = false`. The eviction hand rapidly purges them from the tail without promoting them to MRU. In contrast, LRU's MRU promotions allow one-hit wonders to evict hot items prematurely.

---

### Figure 6: Capacity Threshold Dynamics (Working-Set Phase Transitions)
- **Files**: [`fig6_working_set_transitions.png`](file:///home/chitta/Projects/SIEVE/results/figures/fig6_working_set_transitions.png), [`.pdf`](file:///home/chitta/Projects/SIEVE/results/figures/fig6_working_set_transitions.pdf), [`.svg`](file:///home/chitta/Projects/SIEVE/results/figures/fig6_working_set_transitions.svg)
- **Format**: Dual-panel comparison (Panel A: Loop cliff transition at $C=200$; Panel B: Hot-Scan ceiling at $C=100$).
- **Description**: Demonstrates how cache performance changes relative to discrete working set boundaries.
- **Key Empirical Observations**:
  - **Looping ($K=200$)**: For any capacity strictly less than the loop size ($C \le 128$), cyclic access causes 100% evictions before an item is re-referenced, resulting in a **0.00% hit ratio**. Once $C=256 > 200$, all items fit into cache, yielding **99.00% hit ratio**.
  - **Hot-Scan ($H=100$)**: 50% of requests target a 100-key hot set while 50% are sequential scan keys. For $C < 100$, hit ratio grows linearly with capacity. Once $C \ge 128$, the hot set is fully cached, hitting the theoretical upper asymptote of **49.50%**.

---

### Figure 7: Baselines Without Locality (Uniform Random & Sequential Scan)
- **Files**: [`fig7_uniform_and_scan_baselines.png`](file:///home/chitta/Projects/SIEVE/results/figures/fig7_uniform_and_scan_baselines.png), [`.pdf`](file:///home/chitta/Projects/SIEVE/results/figures/fig7_uniform_and_scan_baselines.pdf), [`.svg`](file:///home/chitta/Projects/SIEVE/results/figures/fig7_uniform_and_scan_baselines.svg)
- **Format**: Dual-panel comparison (Panel A: Uniform random vs theoretical expectation; Panel B: Sequential scan pollution).
- **Description**: Highlights workload regimes where no eviction algorithm can provide a predictive advantage.
- **Key Empirical Observations**:
  - **Uniform Random ($K=1000$)**: All three policies trace the theoretical line $C / K$. At $C=64$, FIFO achieves 6.31%, LRU 6.31%, and SIEVE 6.45% (theoretical = 6.40%). At $C=256$, FIFO achieves 25.47%, LRU 25.56%, and SIEVE 25.36% (theoretical = 25.60%).
  - **Sequential Scan ($N=5000$)**: With zero temporal reuse, all policies exhibit **0.00% hit ratio**, confirming that eviction algorithms cannot manufacture hits when keys are never re-accessed.

---

### Figure 8: Master Executive Summary Dashboard
- **Files**: [`fig8_summary_dashboard.png`](file:///home/chitta/Projects/SIEVE/results/figures/fig8_summary_dashboard.png), [`.pdf`](file:///home/chitta/Projects/SIEVE/results/figures/fig8_summary_dashboard.pdf), [`.svg`](file:///home/chitta/Projects/SIEVE/results/figures/fig8_summary_dashboard.svg)
- **Format**: 2×2 dashboard synthesizing all four primary research dimensions:
  1. Policy comparison bar chart at maximum tested capacity ($C=256$).
  2. Zipf hit ratio scaling curves.
  3. Miss ratio reduction (%) over LRU on Zipf.
  4. Single-threaded simulator replay throughput (MOPS).
- **Description**: The recommended composite graphic for high-level presentations and executive summary sections.

---

## Regeneration Instructions

To regenerate all figures from the raw benchmark data:

```bash
# From the project root:
make plot
# or:
python3 experiments/plot_results.py
```

The script automatically uses the project virtual environment (`.venv`) if system Python lacks Matplotlib.
