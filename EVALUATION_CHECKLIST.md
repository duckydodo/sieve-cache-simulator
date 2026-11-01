# Evaluation Day Pre-Flight Checklist & Presentation Protocol

**Project:** Implementation and Evaluation of the SIEVE Cache Eviction Algorithm (USENIX NSDI 2024)  
**Candidate:** duckydodo (`chitta5828@gmail.com`)  
**Repository:** `/home/chitta/Projects/SIEVE`  

---

## 1. Pre-Flight System Checks (T-30 Minutes)

Perform these sanity checks on your laptop before entering the evaluation room:

- [ ] **Verify Compiler & Tools:**
  ```bash
  gcc --version     # Expect GCC 9+ (supporting -std=c11)
  make --version    # GNU Make
  python3 --version # Python 3.8+
  ```
- [ ] **Verify Clean Build & Zero Warnings:**
  ```bash
  make clean && make all
  ```
  *Check:* Ensure compiler produces zero warnings with `-Wall -Wextra -Wpedantic`.
- [ ] **Verify Automated Verification Suite (51,604 Assertions):**
  ```bash
  make test
  ```
  *Check:* All 5 suites must report `ALL TESTS PASSED` / `OK`.
- [ ] **Verify Pre-Generated Traces and Experiment Data:**
  ```bash
  ls -lh traces/exp/*.txt       # 11 experiment trace files present
  ls -lh results/*.csv          # raw_results.csv and summary.csv present
  ls -lh results/figures/*.png  # 8 publication figures present
  ```
- [ ] **Launch Presentation in Browser:**
  ```bash
  xdg-open PRESENTATION.html 2>/dev/null || firefox PRESENTATION.html &
  ```
  *Check:*
  - Press `F` to toggle fullscreen.
  - Test `←` and `→` arrow keys to cycle slides.
  - Verify embedded figures load on Slide 8, 9, 10, and 12.

---

## 2. Presentation Timing Strategies

Depending on how much time your evaluation panel grants, use one of the two time-tested pacing budgets:

### Plan A: 10-Minute Presentation Budget (Tight / Rapid)

| Time Window | Slide(s) | Topic / Core Focus | Action / Key Message |
| :---: | :---: | :--- | :--- |
| **0:00 – 1:30** | Slides 1–2 | Title, Memory Hierarchy, Problem Statement | Define cache eviction; state why LRU fails on 60–80% one-hit wonders. |
| **1:30 – 3:30** | Slides 3–4 | Policy Comparison & SIEVE Algorithm | Walk through Algorithm 1: in-place hit bit flip vs 6 pointer writes in LRU; lazy demotion. |
| **3:30 – 5:00** | Slides 5–6 | Architecture & Defensive Implementation | Dual structure ($O(1)$ list + load $\le 0.5$ hash table); strict parser; 51,604 assertions. |
| **5:00 – 7:30** | Slides 7–9 | Empirical Results (Zipf, MRR, Latency) | Show Zipf curve: +13.36% hit gain at $C=8$, 20% MRR at $C=64$, 2.34× faster on loop hits. |
| **7:30 – 8:30** | Slide 10 | Working-Set Transitions (Loop cliff, hot-scan) | Explain loop cliff at $C=200$; hot-scan 49.50% ceiling. |
| **8:30 – 9:30** | Slides 11–12 | Limitations, Threats to Validity, Conclusion | Acknowledge single-threaded / synthetic scope; summarize findings. |
| **9:30 – 10:00** | Slide 13 | Final Slide & Transition to Q&A | "Thank you; ready for questions and live demo." |

---

### Plan B: 15-Minute Presentation + Demo Budget (Standard / Comprehensive)

| Time Window | Activity / Slide | Topic / Core Focus |
| :---: | :---: | :--- |
| **0:00 – 2:00** | Slides 1–3 | Introduction, OS Memory Hierarchy, One-Hit Wonders, Policy Comparison. |
| **2:00 – 4:30** | Slides 4–6 | Deep-dive into Algorithm 1, memory ownership model, and pointer survival rule. |
| **4:30 – 8:00** | Slides 7–10 | Thorough walkthrough of empirical results with embedded publication figures. |
| **8:00 – 11:30** | **Live Terminal Demo** | Execute commands from `DEMO_SCRIPT.md`: clean test, live Zipf benchmark, bad trace rejection. |
| **11:30 – 13:30** | Slides 11–12 | Limitations, threats to validity, future multi-threaded extensions. |
| **13:30 – 15:00** | Slide 13 | Concluding remarks & invite viva defense questions. |

---

## 3. Terminal Demo Commands Cheat-Sheet

Keep this terminal open in the background. If asked for a live demonstration, execute these three commands in order:

```bash
# 1. Clean verification test (51,604 assertions in ~2 seconds):
make clean && make all && make test

# 2. Live benchmark across 6 capacities on Zipfian workload:
./bin/benchmark -t traces/exp/zipf_42.txt -c 8,16,32,64,128,256

# 3. Demonstration of strict trace validation (rejection of trailing garbage):
printf "100 trailing_garbage\n200\n" > /tmp/bad.txt && ./bin/benchmark -t /tmp/bad.txt -c 16 ; rm -f /tmp/bad.txt

# 4. Resident loop demonstration showing 2.34x speedup on hits:
./bin/benchmark -t traces/exp/loop_deterministic.txt -c 128,256
```

---

## 4. Fallback & Contingency Plan

| Potential Crisis | Immediate Solution |
| :--- | :--- |
| **Projector cannot display browser properly** | Press `Ctrl + -` or `Ctrl + +` to scale text. Alternatively, use `PRESENTATION.md` directly in VS Code / Marp preview. |
| **Evaluator questions whether data was fabricated** | Point them to `traces/exp/` and run `./bin/benchmark -t traces/exp/zipf_42.txt -c 64` live in front of them to prove the exact numbers match `summary.csv`! |
| **Terminal or compilation error on fresh system** | All results (`results/summary.csv`, `results/raw_results.csv`, `results/figures/`) are pre-generated, static, and stored in git. |
| **Evaluator asks for the full paper / complete report** | Open `REPORT.md` (the comprehensive 12-section technical report) or open the NSDI '24 paper reference link. |
| **Printer or hard-copy requested** | Open `PRESENTATION.html` in browser $\to$ Press `Ctrl + P` $\to$ Destination: "Save as PDF" (pre-formatted with `@media print` slide breaks). |

---

## 5. Instant Defense Key Facts (Keep on Desk)

1. **Why SIEVE beats LRU:** LRU promotes one-hit wonders to the MRU head, displacing hot items. SIEVE inserts at Head and evicts from Tail via lazy demotion—unvisited cold items are swiftly dropped at the tail.
2. **Why SIEVE is faster on hits:** Setting `visited = true` takes **0 pointer writes**. LRU requires **6 pointer updates** on every hit to splice the node to the head.
3. **Worst-case vs Amortized eviction time:** Worst-case is $O(C)$ if all $C$ nodes have `visited == true`. Amortized time is strictly **$O(1)$** because clearing a visited bit ($1 \to 0$) is charged to the hit that set it ($0 \to 1$).
4. **Hash Table load factor:** $\alpha \le 0.5$ guaranteed by sizing buckets to $M = 2^{\lceil \log_2(2C) \rceil}$.
5. **Pointer survival invariant:** In `src/sieve.c`, `next_hand = victim->prev` is saved **before** `victim` is unlinked and freed, preventing a use-after-free bug.
