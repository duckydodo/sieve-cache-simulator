# SIEVE Project — Comprehensive Viva Voce Preparation Guide

**Project Title:** Implementation and Evaluation of the SIEVE Cache Eviction Algorithm  
**Reference Paper:** *“SIEVE is Simpler than LRU: An Efficient Turn-Key Eviction Algorithm for Web Caches”* — Yazhuo Zhang, Paraschos Koutris, Rebecca Bian (USENIX NSDI 2024)  
**Author:** duckydodo (`chitta5828@gmail.com`)  
**Scope:** Single-Threaded C11 User-Space Simulator, Benchmark Runner, and Empirical Evaluation  

---

## Quick Reference Sheet (Memorize for Instant Recall)

| Parameter / Metric | Exact Value in This Project | Rationale / Significance |
| :--- | :--- | :--- |
| **C Standard & Compiler** | C11 (`gcc -std=c11 -Wall -Wextra -Wpedantic`) | Strict standards compliance; 0 warnings |
| **Total Test Assertions** | **51,604 assertions** (all passing) | 479 DS, 320 policies, 50,483 tracegen, 313 benchmark, 9 python |
| **Zipf Skew ($\alpha$)** | $\alpha = 1.0$, $K=1,000$, $N=20,000$ | Standard Zipfian power-law modeling web/CDN object popularity |
| **Evaluated Capacities ($C$)** | $C \in \{8, 16, 32, 64, 128, 256\}$ | Working set coverage $C/K \in [0.8\%, 25.6\%]$ |
| **Peak Hit Gain (SIEVE vs LRU)** | **+13.36%** at $C=8$ (30.96% vs 17.60%) | Peak relative advantage occurs under constrained cache capacity |
| **Miss Ratio Reduction (MRR)** | **14.43% to 20.00%** on Zipf | SIEVE eliminates up to 1/5th of all misses incurred by LRU |
| **Resident Loop Latency ($C=256$)** | LRU: **61.03 ns** (16.39 MOPS) vs SIEVE: **26.09 ns** (38.33 MOPS) | **2.34× speedup** due to 0 pointer writes vs 6 pointer writes |
| **Evictions Saved on Zipf** | **2,671 writes saved** at $C=8$; **1,997 saved** at $C=64$ | Directly protects SSD endurance and backend write-back queues |
| **Loop Cliff Threshold** | $C \le 128$: **0.00% hits**; $C=256$: **99.00% hits** | Sharp phase transition at working set boundary $K=200$ |
| **Hot-Scan Ceiling** | **49.50% hits** at $C \ge 128$ for $H=100$ | 50% streaming scan keys produce 0 hits; all 100 hot keys hit |
| **Hash Table Load Factor** | Max $\alpha \le 0.5$ ($M = 2^{\lceil \log_2(2C) \rceil}$) | Guarantees $O(1)$ average lookup with MurmurHash3 `fmix64` |

---

## Category A: Operating Systems & Cache Fundamentals

### Q A.1: Why is cache eviction considered an Operating Systems problem when this project is a user-space simulator?
**Model Answer:**  
Cache eviction is one of the foundational resource management challenges in operating systems. It governs every tier of the memory hierarchy:
1. **Virtual Memory Page Replacement:** The OS kernel must decide which physical 4 KB DRAM page frame to swap out to disk when memory is exhausted (e.g., Linux's active/inactive LRU page lists in `mm/vmscan.c`).
2. **OS Buffer / Page Cache:** The kernel caches file system disk blocks in DRAM to avoid slow block I/O operations.
3. **Application & Web Caches:** Web reverse proxies (Nginx, Squid, Varnish) and key-value datastores (Redis, Memcached) run in user space but manage large memory pools acting as the front line for backend storage.

We implement this in user space as a deterministic simulator because it enables **exact, cycle-level repeatability**, precise hardware clock isolation, zero kernel interference, and direct verification of algorithmic complexity against identical trace sequences. The algorithmic principles governing eviction—such as temporal locality, one-hit wonder filtering, and eviction queue mechanics—are identical across both domains.

### Q A.2: What is the Principle of Locality, and how do your synthetic workloads test it?
**Model Answer:**  
The Principle of Locality states that computer programs do not access memory uniformly at random, but exhibit two patterns:
- **Temporal Locality:** An object accessed recently is likely to be accessed again in the near future.
- **Spatial Locality:** Objects near each other in address space are likely to be accessed together.

In our cache simulator, keys represent discrete objects (pages or files), so we isolate **temporal locality**:
- `zipf` stresses **heavy temporal skew**: a small fraction of keys account for the majority of requests, but interspersed with a long tail of infrequent keys.
- `uniform` has **zero temporal locality**: each key has identical probability $1/K$, establishing the lower bound where no policy can beat random guessing.
- `loop` tests **strictly periodic temporal reuse**: keys repeat every $K=200$ accesses, demonstrating what happens when loop length exceeds cache capacity.
- `scan` tests **zero temporal reuse**: every access is unique, modeling streaming I/O pollution.
- `hot_scan` tests **mixed locality**: a core working set of 100 hot keys competes with streaming noise.

### Q A.3: What is Bélády's Optimal Algorithm (MIN / OPT), and why can't an OS use it?
**Model Answer:**  
Bélády's Optimal Algorithm (formulated by László Bélády in 1966) proves that the theoretical minimum miss ratio is achieved by evicting the object whose next access is furthest in the future (or will never be accessed again).  
An operating system or online cache **cannot implement MIN** because it requires future clairvoyance—the cache controller only knows past access history, not future requests. MIN serves strictly as an offline theoretical ceiling for evaluating online heuristic policies like LRU, FIFO, and SIEVE.

### Q A.4: What is Bélády's Anomaly? Which algorithms are susceptible to it?
**Model Answer:**  
Bélády's Anomaly is the phenomenon where **increasing cache capacity leads to an increased number of cache misses** for certain access sequences.
- **Stack Algorithms** (such as LRU and LFU) are mathematically **immune** to Bélády's Anomaly. In a stack algorithm, the set of objects in a cache of size $C$ is always a strict subset of the objects in a cache of size $C+1$ at any step $t$ ($S(C, t) \subseteq S(C+1, t)$).
- **FIFO is NOT a stack algorithm** and famously exhibits Bélády's Anomaly (e.g., access string `1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5` produces 9 misses with capacity 3, but 10 misses with capacity 4).
- **SIEVE is NOT a stack algorithm** because its eviction hand positions and visited bit states can diverge between capacity $C$ and $C+1$. While theoretically susceptible to pathological counterexamples, in our extensive evaluations across 5 workloads and 6 capacities, hit ratio grew monotonically with capacity.

---

## Category B: Paper Motivation & Research Context (NSDI 2024)

### Q B.1: What is the "Complexity Crisis" in cache eviction that motivated the SIEVE paper?
**Model Answer:**  
Over the past two decades, researchers attempted to address LRU's flaws by designing increasingly intricate algorithms:
- **ARC (Adaptive Replacement Cache):** Dynamically tunes two double-ended queues (recent vs frequent) and requires "ghost lists" (tracking evicted metadata). Patented by IBM, complex to maintain.
- **2Q & LIRS:** Multi-queue structures with intermediate queues and threshold timers.
- **TinyLFU / W-TinyLFU:** Couples an LRU cache with an admission filter powered by a Count-Min Sketch frequency bloom filter with periodic aging/halving.

**The Crisis:** Despite theoretical hit-ratio gains in academic papers, production systems (Memcached, Redis, Linux kernel) overwhelmingly **refused to adopt them**. Why? Because:
1. High per-object metadata overhead.
2. Complex multi-lock synchronization overhead under multi-core concurrency.
3. Brittle hyperparameters that degrade under shifting workloads.
Zhang et al. (NSDI 2024) asked: *Can we achieve state-of-the-art hit ratios with a design that is simpler than LRU, has zero hyperparameters, and requires no ghost caches?* The result was **SIEVE**.

### Q B.2: What is the "One-Hit Wonder" problem in web caches?
**Model Answer:**  
Empirical studies of production CDNs (Akamai, Cloudflare, Twitter, Wikipedia) demonstrate that **60% to 80% of unique objects requested are accessed exactly once** ("one-hit wonders").  
In LRU, when an object misses, it is inserted at the **MRU Head**. If 80% of incoming requests are one-hit wonders, they flood the head of the queue, pushing genuinely popular items towards the tail and causing them to be evicted prematurely.  
SIEVE addresses this through **lazy demotion**: items enter at the head with `visited = false`. As the eviction hand scans from the tail, items that were never accessed a second time retain `visited = false` and are swiftly discarded, acting as a natural sieve for cold traffic.

### Q B.3: Why does LRU suffer from lock contention on cache hits?
**Model Answer:**  
In LRU, even when a requested item is already resident in cache (a read hit), the node must be moved from its current list position to the MRU Head. This involves:
- Detaching the node (updating `prev->next` and `next->prev`).
- Splicing at head (updating `head->prev`, `node->next`, `node->prev`, and `head`).
This requires **6 pointer writes**. In a multi-threaded system, these pointer writes require acquiring an **exclusive write lock** (or mutex) on the list. Consequently, 100% of read accesses must contend for a write lock, causing multi-core throughput to collapse.  
In SIEVE, a hit only performs an **in-place bit update** (`node->visited = true`). No list pointers change. In a concurrent design, this can be done via atomic bit operations or read locks, allowing parallel reads without serializing through a mutex.

---

## Category C: Policy Comparison & Algorithm Mechanics

### Q C.1: How does SIEVE differ fundamentally from the classic Clock (Second-Chance) algorithm?
**Model Answer:**  
This is a frequent viva question because both algorithms use a 1-bit reference flag. However, they have three decisive differences:
1. **Insertion Location:**
   - **Clock:** Inserts new items at the **current position of the eviction hand** (or replaces the victim in place).
   - **SIEVE:** Always inserts new items at the **Head of the list**, while the eviction hand traverses independently from **Tail towards Head**.
2. **Demotion and Retention Dynamics:**
   - In Clock, a newly inserted item immediately competes with adjacent older items.
   - In SIEVE, new items must traverse the full queue distance from Head to Tail before encountering the eviction hand. This gives recurrent items an entire "lifespan" to receive a second hit and set `visited = true`.
3. **Queue Invariant:**
   - Clock is an unstructured circular ring.
   - SIEVE maintains a strict FIFO ordering from Head to Tail, selectively filtering out unvisited nodes from the tail up.

### Q C.2: Walk through Algorithm 1 of SIEVE step-by-step.
**Model Answer:**  
The algorithm maintains a doubly-linked list (`head`, `tail`), a hash table for $O(1)$ lookups, and a moving pointer `hand`:
1. **On Hit (`key` is present in hash table):**
   - Retrieve `node = ht_get(cache->ht, key)`.
   - Set `node->visited = true`.
   - Do **not** move the node.
2. **On Insert / Miss (`key` not in cache, cache has free capacity):**
   - Allocate `node` with `node->visited = false`.
   - Insert `node` at `head`.
   - If `hand == NULL`, set `hand = tail`.
3. **On Eviction (`key` not in cache, cache is full):**
   - Start at `hand`. If `hand == NULL`, set `hand = tail`.
   - Traverse backwards via `hand->prev`:
     - While `hand->visited == true`:
       - Set `hand->visited = false` (giving it a second chance).
       - Advance `hand = hand->prev` (wrap around to `tail` if `hand == NULL`).
   - When a node with `hand->visited == false` is encountered:
     - **Save next hand position:** `next_hand = hand->prev`.
     - Remove `victim = hand` from list and hash table; free `victim`.
     - Set `cache->hand = next_hand` (if `next_hand == NULL`, wrap to `tail`).
   - Insert new item at `head` with `visited = false`.

### Q C.3: What is "Lazy Demotion" versus "Eager Promotion"?
**Model Answer:**  
- **Eager Promotion (LRU):** Every single access immediately promotes the object to the highest-priority slot (MRU Head), incurring runtime cost and pointer modifications on every hit.
- **Lazy Demotion (SIEVE):** Hits do not move objects; objects retain their FIFO arrival position. Demotion and filtering occur lazily **only during eviction**, when the eviction hand sweeps through the tail. Popular objects survive the sweep by resetting their visited bit, while unvisited objects are discarded.

---

## Category D: Data Structures & Computational Complexity

### Q D.1: Explain the dual data structure architecture of your cache engine.
**Model Answer:**  
Our cache engine (`cache_t` in `src/cache.c`) pairs two complementary data structures:
1. **Hash Table (`ht_t` in `src/hashtable.c`):**
   - Provides $O(1)$ average-time lookup from `uint64_t key` to `dlist_node_t*`.
   - Uses an array of bucket pointers with separate chaining (singly-linked bucket nodes).
   - Array size $M$ is computed as the next power of 2 greater than or equal to $2C$:
     $$M = 2^{\lceil \log_2(2C) \rceil}$$
     Since the maximum number of items in the cache is $C$, this guarantees that the load factor $\alpha = C / M \le 0.5$ at all times, avoiding hash collision degradation.
   - Hash function uses MurmurHash3's 64-bit finalizer (`fmix64`), which delivers complete avalanche properties (every input bit affects every output bit with 50% probability).
2. **Intrusive Doubly-Linked List (`dlist_t` in `src/dlist.c`):**
   - Tracks object ordering with sentinel head and tail pointers.
   - Nodes contain `prev`, `next`, `key`, and `visited` bit.
   - Provides $O(1)$ insertion at head, $O(1)$ removal of an arbitrary node, and $O(1)$ tail access.

### Q D.2: What does "intrusive" data structure mean, and why is it preferred in systems programming?
**Model Answer:**  
In a standard (non-intrusive) list, each list node allocates a separate wrapper that holds a `void *data` pointer to the actual object. This requires two separate heap allocations, incurs cache misses chasing the data pointer, and requires two frees on deletion.  
In an **intrusive list**, the list linkage pointers (`prev`, `next`) are embedded **directly inside the object structure** itself (`dlist_node_t`).  
**Benefits in Systems Programming:**
- **Zero extra allocation:** The node *is* the cache item.
- **$O(1)$ unlinking without list search:** Given a pointer to `dlist_node_t`, we can immediately unlink it from the list (`node->prev->next = node->next`) without needing to traverse the list from the head.
- **Cache locality:** Node pointers and payload data reside on the same memory cache line.

### Q D.3: Prove the time complexity of SIEVE operations. What is its worst-case eviction time?
**Model Answer:**  
- **Hit:** $O(1)$ average time. Hash table lookup takes $O(1)$ (load factor $\le 0.5$), and setting `visited = true` takes $O(1)$.
- **Insert (Not Full):** $O(1)$ time. Allocating node, inserting at list head, and inserting into hash table bucket takes $O(1)$.
- **Eviction Worst-Case:** $O(C)$ time. If all $C$ nodes in the cache currently have `visited == true`, the hand must visit all $C$ nodes, clearing their visited bits to `false`, wrapping around, and selecting the first one.
- **Eviction Amortized Complexity:** Strictly **$O(1)$**.  
  *Proof by Aggregate Method:* A node's `visited` bit can only be set to `true` during a cache hit. Each hit performs exactly one bit flip ($0 \to 1$). During eviction, clearing a bit ($1 \to 0$) takes $O(1)$ work. We can charge the cost of clearing each bit to the hit that originally set it. Over any sequence of $N$ requests, the total number of bit resets cannot exceed the total number of hits ($H \le N$). Thus, the total work spent traversing visited nodes across the entire execution is bounded by $O(N)$, giving an amortized cost per eviction of $O(1)$.

---

## Category E: Implementation Design & Defensive Engineering

### Q E.1: Who owns memory in your implementation, and how do you prevent leaks and double-frees?
**Model Answer:**  
We established a strict **single-ownership memory hierarchy**:
- `cache_t` is the **sole owner** of heap-allocated `dlist_node_t` objects.
- The hash table (`ht_t`) and doubly-linked list (`dlist_t`) are strictly **non-owning indexing structures**. They never call `malloc` or `free` on node pointers.
- To prevent dangling pointers and double frees, each `dlist_node_t` maintains two defensive boolean flags:
  - `node->in_list`: Set to `true` on `dlist_push_head`, cleared on `dlist_remove`.
  - `node->in_ht`: Set to `true` on `ht_put`, cleared on `ht_remove`.
- When `dlist_node_free(node)` is called, it explicitly asserts:
  ```c
  assert(!node->in_list && "Cannot free node still linked in list");
  assert(!node->in_ht && "Cannot free node still referenced in hash table");
  ```
This makes double-free bugs or use-after-free conditions fail fast via assertions rather than causing silent memory corruption. Valgrind confirms **0 leaks and 0 memory errors**.

### Q E.2: Explain the "Hand Invariant" / Pointer Survival Rule in `src/sieve.c`.
**Model Answer:**  
This is a critical bug found in naive implementations of Algorithm 1. In `sieve_evict()`:
```c
/* CRUCIAL: Save hand's next location BEFORE victim is freed! */
dlist_node_t *next_hand = victim->prev;

/* Now safe to remove and free victim */
dlist_remove(cache->list, victim);
ht_remove(cache->ht, victim->key);
dlist_node_free(victim);

/* Update cache hand */
cache->hand = next_hand; /* If next_hand is NULL, next eviction wraps to tail */
```
If a programmer unlinks and frees `victim` before reading `victim->prev`, reading `hand = victim->prev` constitutes a **use-after-free undefined behavior** where `victim->prev` could contain garbage or corrupted memory. Our implementation guarantees that `next_hand` is captured while `victim` is fully intact.

### Q E.3: How does your benchmark runner ensure strict trace input validation?
**Model Answer:**  
In `src/benchmark.c`, `read_trace_keys()` parses raw trace files line by line using `fgets()` and `strtoull()`. To prevent malformed input from corrupting results, it enforces:
1. **Single-token enforcement:** Uses a trailing `char extra` with `sscanf()` to verify that each non-empty line contains exactly one unsigned 64-bit integer and no trailing garbage (e.g., `1 garbage` or `1 2` is immediately rejected).
2. **Range and Sign Validation:** Leading minus signs are rejected; numbers exceeding `UINT64_MAX` trigger `ERANGE` and are rejected.
3. **Clean Teardown:** If an error occurs midway through a 500,000-line file, the parser closes the file handle, frees the allocated array, logs the exact offending line number to `stderr`, and returns an explicit error code.

---

## Category F: Experimental Methodology & Workload Design

### Q F.1: Why did you choose capacities $C \in \{8, 16, 32, 64, 128, 256\}$ for working set $K=1,000$?
**Model Answer:**  
Capacities must be chosen relative to the working set size $K$ to reveal different operational regimes:
- **$C=8, 16$ ($0.8\% - 1.6\%$ coverage):** Highly constrained regime. Stresses the algorithm's ability to retain only the top 1% most critical items while rejecting noise. This is where SIEVE's advantage peaks (+13.36%).
- **$C=32, 64$ ($3.2\% - 6.4\%$ coverage):** Typical web proxy regime. Standard CDNs typically size cache capacity to cover 5% to 10% of total catalog items.
- **$C=128, 256$ ($12.8\% - 25.6\%$ coverage):** Saturated/relaxed regime. As capacity approaches 25%, all algorithms have enough room to hold popular items, and the difference between policies naturally narrows (+3.78%).
By using powers of two spanning two orders of magnitude, we produce smooth, log-scale performance curves that clearly delineate algorithm behavior across all regimes.

### Q F.2: How did you ensure experimental fairness and timing isolation?
**Model Answer:**  
1. **Identical Trace Sequences:** Every policy (FIFO, LRU, SIEVE) processes the **exact same pre-generated file** for each seed. Traces are not generated on the fly during benchmark execution.
2. **Fresh Initialization:** For every capacity and workload, a completely clean `cache_t` is allocated, initialized, and destroyed after execution.
3. **Isolated Timing Boundaries:** In `src/benchmark.c`, timing is captured via `clock_gettime(CLOCK_MONOTONIC)` wrapped strictly around the cache access loop:
   ```c
   clock_gettime(CLOCK_MONOTONIC, &start);
   for (size_t i = 0; i < trace->count; i++) {
       cache_access(cache, trace->keys[i]);
   }
   clock_gettime(CLOCK_MONOTONIC, &end);
   ```
   All trace file reading, memory parsing, and CSV serialization occur outside this timing window. This isolates pure algorithmic latency from disk I/O and standard library formatting.

---

## Category G: Results Interpretation & Empirical Findings

### Q G.1: Why does SIEVE achieve a +13.36% higher hit ratio than LRU on Zipf at $C=8$?
**Model Answer:**  
On Zipfian workloads, a handful of keys (e.g. keys 1, 2, 3) are extremely frequent, but the long tail produces a steady stream of unique or low-frequency keys (one-hit wonders).  
- **Under LRU:** Every time a tail key misses, it is inserted at the **MRU Head**. This displaces existing items towards the Tail. If several tail keys arrive in succession, they push hot keys (e.g., key 1 or 2) all the way to the Tail, where they are evicted. When key 1 is requested again shortly after, it incurs a costly miss.
- **Under SIEVE:** Keys enter at the Head with `visited = false`. The hot keys receive repeated hits and keep `visited = true`. When a miss occurs, the eviction hand checks the tail: unvisited tail keys are immediately evicted, while visited hot keys have their bit reset and are preserved in the cache. Thus, SIEVE acts as a filter that protects hot items from being displaced by cold tail traffic.

### Q G.2: Explain the "Miss Ratio Reduction" (MRR) metric. What does your measured 20.00% MRR mean?
**Model Answer:**  
Miss Ratio Reduction (MRR) measures the proportion of misses eliminated when switching from a baseline policy (LRU) to a new policy (SIEVE):
$$\text{MRR} = \frac{M_{\text{LRU}} - M_{\text{SIEVE}}}{M_{\text{LRU}}} = \frac{(1 - H_{\text{LRU}}) - (1 - H_{\text{SIEVE}})}{1 - H_{\text{LRU}}}$$
At $C=64$ on Zipf:
- LRU hit ratio is $50.10\% \implies M_{\text{LRU}} = 49.90\%$ (9,980 misses out of 20,000).
- SIEVE hit ratio is $60.08\% \implies M_{\text{SIEVE}} = 39.92\%$ (7,984 misses out of 20,000).
- $\text{MRR} = \frac{49.90 - 39.92}{49.90} = \frac{9.98}{49.90} = 20.00\%$.
**Practical Meaning:** In a production web system, backend database queries and origin server fetches are cut by **one full fifth (20%)**, reducing network egress costs and server load.

### Q G.3: Explain the 2.34× runtime speedup of SIEVE over LRU on the resident loop ($C=256$).
**Model Answer:**  
In the looping workload ($K=200$) with capacity $C=256$, the working set fits completely in cache, producing a **99.0% hit ratio**. Under these conditions, performance is dictated entirely by hit-path execution:
- **LRU Hit Path:** Splicing the hit node to the MRU Head requires unlinking it from its current position and re-linking it at the head. This involves writing **6 memory pointers** (`node->prev`, `node->next`, `head->prev`, `head`, etc.), causing memory pipeline stalls and cache line dirtying. Measured latency: **61.03 ns/access** (16.39 MOPS).
- **SIEVE Hit Path:** A hit requires only setting `node->visited = true` in place. Zero pointer writes occur. Measured latency: **26.09 ns/access** (38.33 MOPS).
$$\frac{61.03\text{ ns}}{26.09\text{ ns}} = 2.339 \approx \mathbf{2.34\times\text{ faster}}$$
This demonstrates that even in a single-threaded environment without lock contention, SIEVE's minimal hit overhead delivers substantial performance advantages.

### Q G.4: Explain the step function seen in the Loop workload ($K=200$).
**Model Answer:**  
In `fig6_working_set_transitions.png`, the Loop workload displays an extreme step function:
- For $C=8, 16, 32, 64, 128$: **0.00% hits** for FIFO, LRU, and SIEVE.
- For $C=256$: **99.00% hits** for all three policies.
**Explanation:**  
The loop cycles through keys $0, 1, 2, \dots, 199$ repeatedly. When capacity $C < 200$, by the time key $i$ is accessed again, 199 other keys have been accessed in the interim. Since the cache can hold at most 128 items, key $i$ has always been evicted by the time the loop wraps around. This causes complete cache thrashing.  
Once $C=256 > 200$, all 200 keys fit in the cache. The first 200 accesses incur compulsory cold misses ($200 / 20,000 = 1.0\%$), after which the remaining 19,800 accesses hit resident cache entries ($19,800 / 20,000 = 99.0\%$).

---

## Category H: Limitations & Threats to Validity

### Q H.1: What are the main limitations of your synthetic evaluation?
**Model Answer:**  
1. **Stationary vs Non-Stationary Skew:** Our synthetic Zipf generator generates keys from a stationary distribution with fixed ranks. In real-world CDNs, object popularity drifts continuously (e.g., breaking news, viral tweets) and displays strong diurnal cycles.
2. **Homogeneous Object Sizes:** The simulator evaluates fixed 64-bit integer keys. Real web caches store heterogeneous objects ranging from 100-byte JSON responses to 50 MB video chunks. In variable-size caches, eviction policies must weigh object byte size and miss penalty alongside frequency.
3. **Single-Threaded Model:** To isolate pure algorithmic behavior, our implementation is single-threaded. We did not directly measure multi-threaded rwlock scalability, where the original NSDI '24 paper showed SIEVE scaling up to 7.8× higher throughput than LRU due to lock contention on hits.

### Q H.2: How would SIEVE be implemented in the Linux kernel page cache?
**Model Answer:**  
In the Linux kernel (`mm/vmscan.c`), page reclamation currently uses two LRU lists (Active and Inactive) protected by `pgdat->lru_lock`.
To integrate SIEVE:
1. Replace the dual Active/Inactive lists with a single doubly-linked list (`struct list_head`).
2. Utilize the existing page flag `PG_referenced` as the `visited` bit. When a user process accesses a page via memory mapping, hardware sets the Page Table Entry (PTE) Accessed bit; `kswapd` transfers this to `PG_referenced`.
3. The page reclaim scanner (`shrink_inactive_list`) maintains a cursor pointer (`hand`) that traverses from the tail of the list towards the head:
   - If `PG_referenced` is set: clear the flag and advance the hand.
   - If `PG_referenced` is 0: unmap the page, write back dirty data to swap/disk, and free the page frame.
4. New page allocations enter at the head with `PG_referenced = 0`.
This would eliminate list shuffling on page promotions and reduce `lru_lock` contention during kernel page reclamation.

---

## Category I: High-Difficulty Defense Questions

### Q I.1: Can SIEVE ever perform worse than LRU? Give an exact access pattern where LRU beats SIEVE.
**Model Answer:**  
Yes! SIEVE is not universally superior. It underperforms LRU on access patterns where **pure recency without frequency** is the optimal predictor.
**Concrete Example (Moving Window of Locality):**  
Consider an access sequence where keys are accessed in phases of size $C$, where each phase accesses a new set of keys with temporal reuse within the phase, but no reuse across phases.  
Furthermore, consider a loop of size $C + 1$:
$$1, 2, \dots, C, C+1, 1, 2, \dots$$
Under certain hand positions, SIEVE's hand sweeps and resets visited bits, but because items do not get promoted to the head on hits, a frequently accessed item located near the tail can be evicted before a newly inserted unvisited item at the head. LRU guarantees that any item touched *immediately* moves to the safest position (Head), which gives LRU an advantage on strictly recency-driven workloads without power-law frequency skew.

### Q I.2: Since Clock also uses a 1-bit visited flag and requires 0 pointer moves on hits, why didn't the paper authors compare SIEVE to Clock instead of LRU?
**Model Answer:**  
The authors *did* compare SIEVE to Clock in Section 4 and 5 of the paper! They found that **Clock's hit ratio is significantly worse than SIEVE's** on web traces (often matching or trailing FIFO).  
**Why Clock fails where SIEVE succeeds:**
In Clock, when an item is evicted, the new item is inserted directly at the **hand position**. This means the new item is placed right in the path of the advancing eviction hand. If the hand sweeps again soon, the new item is evicted before it has time to receive a second hit.  
In SIEVE, new items are always inserted at the **Head**, while the hand is at the **Tail**. The new item has the entire length of the cache to accumulate hits before the hand ever reaches it. This structural separation of the **insertion point** (Head) and the **eviction point** (Tail) is the key innovation that enables lazy demotion.

### Q I.3: If you had 2 more weeks on this project, what exact experiment would you run next?
**Model Answer:**  
I would implement a **multi-threaded concurrent benchmark using POSIX `pthread_rwlock_t`**:
1. Spawn 2, 4, 8, and 16 worker threads concurrently issuing lookups from pre-partitioned trace slices.
2. In LRU, each hit must acquire a write lock (`pthread_rwlock_wrlock`) to splice the node to the head.
3. In SIEVE, each hit only needs a read lock (`pthread_rwlock_rdlock`) to locate the node in the hash table, followed by an atomic bit operation (`atomic_fetch_or(&node->visited, 1)`). Only the eviction path on a cache miss acquires the write lock.
4. Measure throughput (Requests Per Second) as core count scales. This would directly reproduce the paper's finding that SIEVE achieves up to **7.8× higher throughput than LRU under multi-threaded concurrency**.

---

## Final Viva Advice
- **Be upfront about scope:** You built a clean, single-threaded C11 simulator to isolate algorithmic complexity and cache dynamics, not a multi-threaded CDN server.
- **Reference your numbers with confidence:** Zipf $C=64$ (60.08% vs 50.10%), Resident Loop $C=256$ (26.09 ns vs 61.03 ns, 2.34× speedup), 51,604 passing test assertions.
- **Defend your engineering:** Highlight the single-ownership memory model, load factor $\le 0.5$ hash table, MurmurHash3 mix, strict trace parsing, and zero compiler warnings.
