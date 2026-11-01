#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct benchmark_config {
    const char *trace_file;
    const char *output_file;
    size_t capacity;          /* Primary / single capacity */
    const size_t *capacities; /* Optional array of multiple capacities */
    size_t num_capacities;    /* Number of capacities in array */
    const char *workload;
    uint64_t seed;
    bool seed_provided;
} benchmark_config_t;

size_t count_distinct_keys(const uint64_t *keys, size_t count);
int read_trace_keys(const char *filename, uint64_t **out_keys, size_t *out_count);
int run_benchmark(FILE *out, const benchmark_config_t *cfg,
                   const uint64_t *keys, size_t key_count);

#endif /* BENCHMARK_H */
