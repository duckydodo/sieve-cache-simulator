# SIEVE Cache Simulator

A user-space cache simulator implementing FIFO, LRU, and SIEVE eviction algorithms,
based on the research paper *"SIEVE is Simpler than LRU: An Efficient Turn-Key Eviction
Algorithm for Web Caches"* (Zhang et al., USENIX NSDI 2024).

## Project Structure

```
SIEVE/
├── include/               # Public headers
│   ├── cache.h            # Cache manager and policy interface
│   ├── benchmark.h        # Benchmark runner interface
│   ├── dlist.h            # Doubly linked list
│   ├── hashtable.h        # Fixed-bucket hash table
│   └── tracegen.h         # Trace generator
├── src/                   # Implementation
│   ├── cache.c            # Cache manager
│   ├── fifo.c             # FIFO eviction policy
│   ├── lru.c              # LRU eviction policy
│   ├── sieve.c            # SIEVE eviction policy (NSDI '24 Algorithm 1)
│   ├── dlist.c            # Doubly linked list
│   ├── hashtable.c        # Hash table (MurmurHash3 fmix64)
│   ├── tracegen.c         # Trace generator library
│   └── benchmark.c        # Benchmark runner
├── tests/                 # Unit and integration tests
│   ├── test_ds.c          # Data structure tests (479 assertions)
│   ├── test_policies.c    # Cache policy tests (320 assertions)
│   ├── test_tracegen.c    # Trace generator tests (50,483 assertions)
│   └── test_benchmark.c   # Benchmark tests (216 assertions)
├── tools/                 # Standalone CLI tools
│   └── tracegen.c         # Trace generator CLI
├── experiments/           # Milestone 6 experiment scripts
│   ├── run_experiment.py  # Full experiment runner (traces → CSV → summary)
│   └── plot_results.py    # Graph generator (requires matplotlib)
├── traces/exp/            # Generated trace files (created by run_experiment.py)
├── results/               # Experiment outputs
│   ├── raw_results.csv    # All benchmark rows
│   ├── summary.csv        # Mean/std hit ratios per workload/capacity/policy
│   ├── ANALYSIS.md        # Experimental analysis and interpretation
│   └── figures/           # Visual plots (created by plot_results.py when matplotlib is installed)
├── Makefile
└── README.md
```

## Building

```bash
make clean
make all
```

The project uses C11 with strict warnings: `-std=c11 -Wall -Wextra -Wpedantic`.

## Testing

Run all test suites:

```bash
make test
```

This executes:
- Data structure tests (479 assertions)
- Cache policy tests (320 assertions)
- Trace generator tests (50,483 assertions)
- Benchmark tests

## Trace Generation

Generate synthetic cache access traces using the tracegen tool:

```bash
./bin/tracegen -w uniform -n 10000 -k 1000 -s 42 -o traces/uniform.txt
./bin/tracegen -w zipf -n 10000 -k 1000 -a 1.0 -s 42 -o traces/zipf.txt
./bin/tracegen -w loop -n 10000 -k 500 -o traces/loop.txt
./bin/tracegen -w scan -n 10000 -o traces/scan.txt
./bin/tracegen -w hot_scan -n 10000 --hot-size 100 --hot-frac 0.5 -o traces/hot_scan.txt
```

### Workload Types

- `uniform`: Random accesses across key range without modulo bias
- `zipf`: Skewed accesses following Zipf distribution with parameter alpha
- `loop`: Repeated sequential cycling through working set
- `scan`: Sequential one-pass access
- `hot_scan`: Repeated accesses to hot set followed by sequential scan

### Tracegen Options

- `-w, --workload <type>`: Workload type (default: uniform)
- `-n, --accesses <count>`: Number of access requests (default: 10000)
- `-k, --keys <count>`: Key range or working set size (default: 1000)
- `-s, --seed <number>`: PRNG seed (default: 42)
- `-a, --alpha <float>`: Zipf skew parameter (default: 1.0)
- `--hot-size <count>`: Hot set size for hot_scan (default: 50)
- `--hot-frac <float>`: Fraction of requests in hot phase (default: 0.5)
- `-o, --output <file>`: Output file path (default: stdout)
- `-h, --help`: Show help message

## Benchmark Runner

Replay a trace through FIFO, LRU, and SIEVE policies and compare metrics:

```bash
./bin/benchmark -t traces/uniform.txt -c 64 -o results/uniform.csv
```

### Benchmark CLI Options

- `-t, --trace <file>`: Input trace file (required, one uint64_t key per line)
- `-c, --capacity <size>`: Cache capacity (required, > 0)
- `-o, --output <file>`: Output CSV file (default: stdout, '-' for stdout)
- `-w, --workload <name>`: Workload name for metadata (optional)
- `-s, --seed <number>`: Seed for metadata (optional)
- `-h, --help`: Show help message

### CSV Output Format

The benchmark outputs a CSV with the following columns:

| Column | Description |
|--------|-------------|
| workload | Workload name (or "unknown") |
| trace | Trace filename |
| seed | Seed value (0 if not provided) |
| accesses | Total number of cache accesses |
| capacity | Cache capacity |
| policy | Policy name (FIFO, LRU, or SIEVE) |
| hits | Number of cache hits |
| misses | Number of cache misses |
| evictions | Number of evictions |
| hit_ratio | Hit ratio (hits / accesses) |
| miss_ratio | Miss ratio (misses / accesses) |
| elapsed_ns | Elapsed time in nanoseconds |

### Example Output

```csv
workload,trace,seed,accesses,capacity,policy,hits,misses,evictions,hit_ratio,miss_ratio,elapsed_ns
uniform,traces/uniform.txt,42,10000,64,FIFO,1234,8766,8702,0.123400,0.876600,1234567
uniform,traces/uniform.txt,42,10000,64,LRU,1567,8433,8369,0.156700,0.843300,1456789
uniform,traces/uniform.txt,42,10000,64,SIEVE,1789,8211,8147,0.178900,0.821100,1345678
```

### Example End-to-End Experiment

Generate a trace and benchmark all policies:

```bash
# Create traces directory
mkdir -p traces results

# Generate a uniform trace
./bin/tracegen -w uniform -n 10000 -k 1000 -s 42 -o traces/uniform.txt

# Benchmark at capacity 64
./bin/benchmark -t traces/uniform.txt -c 64 -w uniform -s 42 -o results/uniform_c64.csv

# Benchmark at capacity 128
./bin/benchmark -t traces/uniform.txt -c 128 -w uniform -s 42 -o results/uniform_c128.csv

# Benchmark at capacity 256
./bin/benchmark -t traces/uniform.txt -c 256 -w uniform -s 42 -o results/uniform_c256.csv
```

## Experiments

Milestone 6 provides automated scripts in `experiments/` to evaluate FIFO, LRU, and SIEVE across multiple workloads, seeds, and cache capacities, compute aggregated statistics, and generate comparison plots.

### Running Experiments

Run the full evaluation matrix (5 workloads × 6 capacities × multiple seeds = 198 benchmark runs):

```bash
make experiment
# or: python3 experiments/run_experiment.py
```

Run a quick reduced-size evaluation (smaller trace sizes and fewer capacities) for fast verification:

```bash
make experiment-quick
# or: python3 experiments/run_experiment.py --quick
```

Regenerate the Markdown analysis from existing summary CSV data without re-running benchmarks:

```bash
python3 experiments/run_experiment.py --analysis-only
```

### Experiment Outputs

Results and traces are written to the following locations:

| Path | Description |
|------|-------------|
| `results/raw_results.csv` | Full benchmark records for every policy, capacity, and seed (198 rows) |
| `results/summary.csv` | Mean and standard deviation metrics grouped by workload, capacity, and policy (90 rows) |
| `results/ANALYSIS.md` | In-depth experimental analysis, findings, workload behavior, and limitations |
| `traces/exp/` | Synthetic trace files generated for the evaluation matrix |
| `results/figures/` | Destination folder for visual plots (generated when running `make plot`) |

### Plotting and Matplotlib Dependency

Data collection, CSV generation, and analysis do not require any external Python packages (they use only standard C and Python libraries).

Generating graphical plots (`make plot`) requires `matplotlib`. To install `matplotlib` in a reproducible project-local virtual environment:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install matplotlib
```

Once `matplotlib` is installed, generate the figures:

```bash
make plot
# or: python3 experiments/plot_results.py
```

When generated, figures are saved to `results/figures/`:
- `results/figures/hit_ratio_vs_capacity.png`: Hit ratio vs cache capacity across all workloads
- `results/figures/policy_comparison.png`: Policy comparison bar chart at maximum capacity
- `results/figures/execution_time.png`: Mean execution runtime per workload and policy

> [!NOTE]
> Figures are not created by default until `make plot` (or `python3 experiments/plot_results.py`) is run with `matplotlib` installed. If `matplotlib` is missing, `make plot` exits with clear installation instructions, while `make experiment` completes CSV and analysis generation normally.

## Implementation Notes

### Cache Keys
- Uses 64-bit unsigned integers (uint64_t)
- Supports the full range including 0 and UINT64_MAX

### Data Structures
- **Doubly linked list**: O(1) head/tail operations
- **Fixed-bucket hash table**: Power-of-2 buckets, separate chaining
- Bucket count: `num_buckets >= 2 * capacity AND num_buckets >= 16`
- Average load factor bounded to <= 0.5

### SIEVE Implementation
- Follows Algorithm 1 from the NSDI 2024 paper
- Hand-sifting with visited bits for lazy promotion
- Handles wraparound when all entries have visited=true
- New nodes enter at list head (newest/MRU end)
- Tail is the oldest/LRU end

### Memory Management
- Cache manager exclusively owns all dlist_node_t allocations
- Hash table and doubly linked list are non-owning indexing structures
- Strict ownership contract enforced by `dlist_node_free()`

## Dependencies

- C11 compiler (`gcc` or `clang`)
- Standard C library
- POSIX `clock_gettime()` for timing
- `libm` for math functions (tracegen only)
- Python 3.8+ (standard library only for experiment runner and CSV analysis)
- `matplotlib` (optional, required only for generating visual plots via `make plot`)

## License

This is an academic project for course work. The SIEVE algorithm is described in the NSDI 2024 paper by Zhang et al.
