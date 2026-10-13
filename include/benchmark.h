#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct benchmark_config {
    const char *trace_file;
    const char *output_file;
    size_t capacity;
    const char *workload;
    uint64_t seed;
    bool seed_provided;
} benchmark_config_t;

int read_trace_keys(const char *filename, uint64_t **out_keys, size_t *out_count);
int run_benchmark(FILE *out, const benchmark_config_t *cfg,
                   const uint64_t *keys, size_t key_count);

#endif /* BENCHMARK_H */
