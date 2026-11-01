CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g
LDFLAGS ?= -lm
SAN_FLAGS ?= -fsanitize=address,undefined

DS_SRCS = src/dlist.c src/hashtable.c
POLICY_SRCS = src/cache.c src/fifo.c src/lru.c src/sieve.c $(DS_SRCS)
TRACE_SRCS = src/tracegen.c
BENCHMARK_SRCS = src/benchmark.c $(POLICY_SRCS)

.PHONY: all asan test experiment plot clean

all: bin/test_ds bin/test_policies bin/test_tracegen bin/tracegen bin/benchmark

bin/test_ds: tests/test_ds.c $(DS_SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^

bin/test_policies: tests/test_policies.c $(POLICY_SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^

bin/test_tracegen: tests/test_tracegen.c $(TRACE_SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

bin/tracegen: tools/tracegen.c $(TRACE_SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

bin/benchmark: $(BENCHMARK_SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

bin/test_benchmark: tests/test_benchmark.c src/benchmark.c $(POLICY_SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -DBENCHMARK_NO_MAIN -o $@ $^

asan: CFLAGS += $(SAN_FLAGS)
asan: clean all

test: bin/test_ds bin/test_policies bin/test_tracegen bin/tracegen bin/test_benchmark
	./bin/test_ds
	./bin/test_policies
	./bin/test_tracegen
	./bin/test_benchmark
	python3 -m unittest tests/test_experiment_runner.py

# Run the full Milestone 6 experiment (generates traces, benchmarks, and CSVs)
experiment: all
	python3 experiments/run_experiment.py

# Quick reduced-size experiment (fast check, same code paths)
experiment-quick: all
	python3 experiments/run_experiment.py --quick

# Generate graphs from existing results/summary.csv (requires matplotlib)
plot:
	python3 experiments/plot_results.py

clean:
	rm -rf bin obj
