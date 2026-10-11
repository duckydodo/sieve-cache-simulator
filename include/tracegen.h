#ifndef TRACEGEN_H
#define TRACEGEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Workload types supported by tracegen */
typedef enum workload_type {
    WORKLOAD_UNIFORM,
    WORKLOAD_ZIPF,
    WORKLOAD_LOOP,
    WORKLOAD_SCAN,
    WORKLOAD_HOT_SCAN
} workload_type_t;

/* Portable 64-bit pseudo-random number generator (SplitMix64) */
typedef struct prng {
    uint64_t state;
} prng_t;

void prng_seed(prng_t *rng, uint64_t seed);
uint64_t prng_next64(prng_t *rng);
uint64_t prng_uniform_range(prng_t *rng, uint64_t range);
double prng_uniform_double(prng_t *rng);

/* Trace generator configuration */
typedef struct trace_config {
    workload_type_t workload;
    uint64_t num_accesses;   /* Total number of accesses to generate */
    uint64_t key_range;      /* Working set size or distinct key range */
    uint64_t seed;           /* PRNG seed */
    double alpha;            /* Zipf skew parameter (> 0.0) */
    uint64_t hot_size;       /* Size of hot set for hot_scan workload */
    double hot_fraction;     /* Fraction of accesses in hot phase (0.0 < frac < 1.0) */
    const char *output_file; /* Path to output file, or NULL for stdout */
} trace_config_t;

/* Initialize configuration with default values for a given workload */
void trace_config_init_defaults(trace_config_t *cfg, workload_type_t type);

/* Validate configuration, returns 0 if valid or -1 with error message */
int trace_config_validate(const trace_config_t *cfg, char *err_msg, size_t err_len);

/* Parse workload name string to workload_type_t enum (returns -1 if unknown) */
int trace_parse_workload_name(const char *name, workload_type_t *out_type);

/* Human-readable workload name */
const char *trace_workload_name(workload_type_t type);

/*
 * Generate trace writing plain-text keys (one uint64_t per line) to a stream.
 * Returns 0 on success, -1 on error.
 */
int trace_generate_stream(const trace_config_t *cfg, FILE *out);

/*
 * Generate trace writing to a file path (or stdout if path is NULL or "-").
 * Returns 0 on success, -1 on error.
 */
int trace_generate_file(const trace_config_t *cfg, const char *path);

/*
 * Generate trace writing into an in-memory array of uint64_t keys.
 * Returns 0 on success, -1 on error.
 */
int trace_generate_array(const trace_config_t *cfg, uint64_t *out_array, size_t count);

#endif /* TRACEGEN_H */
