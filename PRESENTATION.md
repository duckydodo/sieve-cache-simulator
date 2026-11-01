---
marp: true
theme: default
paginate: true
header: "SIEVE Cache Eviction Algorithm — USENIX NSDI 2024"
footer: "Undergraduate Operating Systems Project | duckydodo <chitta5828@gmail.com>"
style: |
  section {
    font-family: 'Helvetica Neue', Arial, sans-serif;
    color: #2c3e50;
    background-color: #ffffff;
    font-size: 22px;
    padding: 40px;
  }
  h1 {
    color: #1a365d;
    font-size: 34px;
    margin-bottom: 15px;
  }
  h2 {
    color: #2b6cb0;
    font-size: 26px;
    margin-bottom: 10px;
  }
  table {
    font-size: 18px;
    border-collapse: collapse;
    width: 100%;
    margin-top: 15px;
  }
  th {
    background-color: #2b6cb0;
    color: white;
    padding: 8px 12px;
  }
  td {
    border: 1px solid #e2e8f0;
    padding: 8px 12px;
  }
  .highlight {
    color: #2ca02c;
    font-weight: bold;
  }
  .alert {
    color: #c53030;
    font-weight: bold;
  }
  code {
    background-color: #edf2f7;
    color: #805ad5;
    padding: 2px 6px;
    border-radius: 4px;
  }
  .cols {
    display: flex;
    justify-content: space-between;
  }
  .col {
    flex: 1;
    margin: 0 10px;
  }
---

<!-- Slide 1: Title & Project Identity -->
# Implementation and Evaluation of the SIEVE Cache Eviction Algorithm
### A Comparative Systems Study of FIFO, LRU, and SIEVE Eviction Dynamics

**Author:** duckydodo (`chitta5828@gmail.com`)  
**Context:** Undergraduate Operating Systems Project  
**Reference Paper:** *“SIEVE is Simpler than LRU: An Efficient Turn-Key Eviction Algorithm for Web Caches”*  
Yazhuo Zhang, Paraschos Koutris, Rebecca Bian (USENIX NSDI 2024)

---

<!-- Slide 2: Problem Statement & Motivation -->
# The Operating Systems Problem: Cache Eviction

### The Memory Hierarchy Challenge
- Fast storage (SRAM, DRAM) is strictly capacity-bounded; backing storage (SSD, Disk, Remote Web) is orders-of-magnitude slower.
- When cache capacity $C$ is exhausted, an eviction policy must select a victim item to discard.

### Why LRU Falls Short in Modern Systems
1. **One-Hit Wonder Vulnerability:**
   - In web and key-value workloads, **60%–80% of items are accessed only once**.
   - LRU inserts new items at the Most Recently Used (MRU) head, allowing non-recurrent objects to flush valuable, frequently-accessed items.
2. **Lock Contention on Cache Hits:**
   - Every hit in LRU requires unlinking and re-inserting nodes at the head (6 pointer writes).
   - In concurrent systems, every read access turns into a structural write, creating a severe multi-core synchronization bottleneck.

---

<!-- Slide 3: Background: FIFO vs. LRU vs. SIEVE -->
# Background: Comparing Eviction Paradigms

| Dimension | FIFO (First-In, First-Out) | LRU (Least Recently Used) | SIEVE (Zhang et al. 2024) |
| :--- | :--- | :--- | :--- |
| **Hit Operation** | No-op ($O(1)$) | Spliced to MRU Head ($O(1)$) | **In-place bit update (`visited = 1`)** ($O(1)$) |
| **Pointer Writes on Hit** | **0** | **6 pointer updates** | **0 pointer updates** |
| **Eviction Candidate** | Tail node ($O(1)$) | Tail node ($O(1)$) | **Moving hand scan** (Amortized $O(1)$) |
| **Metadata per Object** | Zero | 2 list pointers | **1 bit (`visited`)** + 2 list pointers |
| **One-Hit Wonder Handling**| Poor | Extremely Vulnerable | **Quick tail discard (Lazy Demotion)** |
| **Worst-Case Eviction** | $O(1)$ | $O(1)$ | **$O(C)$** *(if all entries visited)* |

---

<!-- Slide 4: SIEVE Algorithm Step-by-Step -->
# The SIEVE Algorithm: Step-by-Step

```
[HEAD]                                               [TAIL]
  |                                                    |
  v                                                    v
[ Node E ] <-> [ Node D ] <-> [ Node C ] <-> [ Node B ] <-> [ Node A ]
 v = 0          v = 1          v = 0          v = 1          v = 1
                                                             ^
                                                             |-- Eviction Hand
```

1. **Insertion:** New misses enter at the **Head** with `visited = false`. Hand initialized to Tail.
2. **Hit:** Read-only bit flip: `node->visited = true`. **Zero list pointer mutations**.
3. **Eviction:** Hand traverses from Tail towards Head via `prev`:
   - If `node->visited == true`: clear `visited = false`, advance `hand = hand->prev`.
   - If `node->visited == false`: **Victim selected!**
   - Save next position: `hand = victim->prev` **before** victim excision.
4. **All-Visited Wraparound:** If hand reaches Head without finding an unvisited node, it wraps back to Tail and repeats.

---

<!-- Slide 5: System Architecture -->
# System Design and Architecture

Modular, single-threaded C11 user-space cache simulator with clean component decoupling:

```
+--------------------------------------------------------------------------+
|                       Benchmark Runner (bin/benchmark)                   |
|   Strict Line Parser (rejects junk)  |  RFC 4180 CSV  | Isolated Timing  |
+--------------------------------------------------------------------------+
                                    |
                                    v
+--------------------------------------------------------------------------+
|                            Cache Engine (cache_t)                        |
|   Single Memory Owner | Statistics Tracking (hits, misses, evictions)    |
+--------------------------------------------------------------------------+
          |                                            |
          v                                            v
+------------------------------------+   +---------------------------------+
|  Fixed-Bucket Hash Table (ht_t)    |   | Intrusive Doubly Linked List    |
|  - Power-of-2 sizing (load <= 0.5) |   | - O(1) head/tail operations     |
|  - MurmurHash3 finalizer (fmix64)  |   | - Intrusive dlist_node_t        |
+------------------------------------+   +---------------------------------+
                                    |
                                    v
+--------------------------------------------------------------------------+
|             Polymorphic Policy Interface (cache_policy_ops_t)            |
|       FIFO (fifo_ops)    |    LRU (lru_ops)    |    SIEVE (sieve_ops)    |
+--------------------------------------------------------------------------+
```

---

<!-- Slide 6: Implementation & Defensive Engineering -->
# Implementation Details & Defensive Engineering

### Robust Memory Ownership Contract
- `cache_t` exclusively owns all node allocations.
- Doubly linked list and hash table are strictly **non-owning index structures**.
- Nodes maintain defensive flags `in_list` and `in_ht`; `dlist_node_free()` asserts complete detachment before freeing.

### Complexity Guarantees
- **Lookup:** Average $O(1)$ via fixed-bucket hash table (load factor bounded $\le 0.5$).
- **Hit:** Strictly $O(1)$ across all policies.
- **Eviction:** Strictly $O(1)$ for FIFO/LRU; amortized $O(1)$ with $O(C)$ worst case for SIEVE.

### Rigorous Test Verification (51,604 Checks)
- **Data Structures (`bin/test_ds`):** 479 assertions.
- **Cache Policies (`bin/test_policies`):** 320 assertions.
- **Trace Generator (`bin/test_tracegen`):** 50,483 assertions.
- **Benchmark Runner (`bin/test_benchmark`):** 313 assertions.
- **Python Experiment Suite:** 9 regression tests. **Zero compiler warnings.**

---

<!-- Slide 7: Experimental Methodology -->
# Experimental Methodology & Workloads

### Synthetic Workload Matrix (Stress-Testing Locality Primitives)
- **`zipf` ($N=20,000, K=1,000, \alpha=1.0$):** Heavy-tailed popularity skew; models web/CDN caching.
- **`uniform` ($N=20,000, K=1,000$):** Uniform random i.i.d. requests; models absence of locality.
- **`loop` ($N=20,000, K=200$):** Cyclic sequential accesses; tests working-set capacity boundaries.
- **`scan` ($N=5,000$):** Single-pass sequential scan; models streaming data pollution.
- **`hot_scan` ($N=20,000$):** 50% accesses to 100 hot keys + 50% non-repeating streaming scan.

### Evaluation Protocol
- **Capacities:** $C \in \{8, 16, 32, 64, 128, 256\}$ (0.8% to 25.6% working set coverage).
- **Repetitions:** 3 independent seeds (`42`, `1337`, `99991`) for stochastic workloads; deterministic replay.
- **Isolated Timing:** POSIX `clock_gettime(CLOCK_MONOTONIC)` wraps cache access loop only. Excludes file I/O, parsing, and CSV printing.

---

<!-- Slide 8: Main Hit-Ratio Results -->
# Main Results: Hit Ratio on Skewed Workloads

### Complete Capacity Progression on Zipf ($\alpha = 1.0$)

| Capacity ($C$) | Coverage ($C/K$) | FIFO Hit % | LRU Hit % | SIEVE Hit % | **SIEVE Gain vs LRU** | **Miss Ratio Reduction** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **8** | 0.8% | 15.44% | 17.60% | **30.96%** | <span class="highlight">+13.36%</span> | <span class="highlight">+16.21%</span> |
| **16** | 1.6% | 23.56% | 27.77% | **41.09%** | <span class="highlight">+13.32%</span> | <span class="highlight">+18.44%</span> |
| **32** | 3.2% | 33.52% | 38.83% | **50.37%** | <span class="highlight">+11.54%</span> | <span class="highlight">+18.86%</span> |
| **64** | 6.4% | 44.44% | 50.10% | **60.08%** | <span class="highlight">+9.98%</span> | <span class="highlight">+20.00%</span> |
| **128** | 12.8% | 56.31% | 61.53% | **68.83%** | <span class="highlight">+7.30%</span> | <span class="highlight">+18.98%</span> |
| **256** | 25.6% | 69.12% | 73.80% | **77.58%** | <span class="highlight">+3.78%</span> | <span class="highlight">+14.43%</span> |

- **Peak Gain at Small Capacities:** Advantage is largest when cache space is constrained.
- **Miss Ratio Reduction (MRR):** SIEVE eliminates **14.4% to 20.0% of all misses** that occur under LRU.

---

<!-- Slide 9: Runtime & Eviction Insights -->
# Runtime Performance & Eviction Dynamics

### 1. Pointer Mutation Penalty in LRU (Loop Workload, $C=256$)
- In fully resident loop access (**99.0% hit ratio**):
  - **LRU:** Splicing nodes on every hit requires 6 pointer writes $\rightarrow$ **61.03 ns/access (16.39 MOPS)**.
  - **SIEVE:** Setting `visited = true` in-place requires 0 pointer writes $\rightarrow$ **26.09 ns/access (38.33 MOPS)**.
  - **Outcome:** SIEVE is **2.34× faster than LRU** on hit-dominated execution!

### 2. Eviction Volume Suppression (Storage Write-Back Savings)
- Invariant confirmed: $\text{evictions} = \text{misses} - C$ once cache is filled.
- Higher hits directly suppress storage write-back traffic:
  - At $C=8$: SIEVE saves **2,671 evictions vs LRU** (13,800 vs 16,471).
  - At $C=64$: SIEVE saves **1,997 evictions vs LRU** (7,919 vs 9,916).
  - **Takeaway:** Directly preserves SSD flash endurance and cuts backend write amplification.

---

<!-- Slide 10: Capacity Thresholds & Baselines -->
# Working-Set Transitions & Baselines

<div class="cols">
<div class="col">

### The Loop Cliff (`loop`, $K=200$)
- **$C \le 128 < 200$:** Total thrashing. Items evicted before cycle repeats $\rightarrow$ **0.00% hits** across all policies.
- **$C = 256 > 200$:** Working set fully fits in cache $\rightarrow$ **99.00% hits** (initial 200 cold misses).

### Scan Pollution (`scan`, $N=5,000$)
- Single-pass access with zero reuse $\rightarrow$ **0.00% hits** for all policies.
- Confirms eviction policies cannot manufacture hits without temporal reuse.

</div>
<div class="col">

### Hot-Scan Ceiling (`hot_scan`, $H=100$)
- 50% hot-set accesses + 50% streaming scan.
- $C < 100$: Linear growth.
- $C \ge 128 > 100$: Reaches theoretical **49.50% ceiling** across all policies.

### Uniform Baseline (`uniform`, $K=1,000$)
- Traces theoretical probability $C / K$:
  - $C=8$: Theor 0.8% | Obs **0.83%**
  - $C=64$: Theor 6.4% | Obs **6.45%**
  - $C=256$: Theor 25.6% | Obs **25.36%**

</div>
</div>

---

<!-- Slide 11: Limitations & Threats to Validity -->
# Limitations and Threats to Validity

### 1. Synthetic Workload Assumptions
- **Static vs. Non-Stationary Skew:** Synthetic Zipf assumes static key ranks; real CDN workloads exhibit diurnal popularity drift and viral shifts.
- **Homogeneous Objects:** Evaluates fixed 64-bit keys; real web caches manage heterogeneous byte payloads (100B to 10MB) and variable TTL expirations.

### 2. User-Space vs. Kernel-Space Implementation
- Simulator operates entirely in user-space RAM.
- Does not model virtual memory page tables, TLB invalidation shootdowns, or block I/O interrupt latency.

### 3. Concurrency Model
- Simulator is single-threaded to isolate pure algorithmic complexity.
- Does not directly measure multi-threaded rwlock contention (where the NSDI '24 paper showed SIEVE scaling up to 7.8× over LRU).

---

<!-- Slide 12: Conclusion & Future Work -->
# Conclusion and Future Directions

### Summary of Evidence-Backed Findings
1. **SIEVE is empirically superior to LRU on skewed web workloads**, achieving up to **+13.36% higher hit ratio** and cutting misses by **up to 20.00%**.
2. **SIEVE eliminates pointer mutation overhead on hits**, executing up to **2.34× faster than LRU** in hit-dominated execution.
3. **SIEVE is simpler than LRU**, requiring only 1 bit per object and a moving eviction hand over a FIFO queue.

### Realistic Future Extensions
- **Multi-Threaded Benchmark:** Measure read/write lock contention under parallel worker threads.
- **Admission Filtering:** Pair SIEVE with TinyLFU frequency sketches to reject cold items at the cache ingress.
- **Production Traces:** Evaluate performance on multi-gigabyte Akamai, Wikimedia, or SNIA block traces.

---

<!-- Slide 13: Questions -->
# Questions & Discussion

### Project Summary
- **Source Code:** C11 (`-std=c11 -Wall -Wextra -Wpedantic`), 0 warnings.
- **Test Suite:** 51,604 assertions passing across 5 test suites.
- **Artifacts:** Full benchmark data (`results/raw_results.csv`, `summary.csv`), 8 publication figures in PNG/PDF/SVG, comprehensive technical report (`REPORT.md`).

**Thank you! Open for questions.**
