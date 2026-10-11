#include "tracegen.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ZIPF_KEYS UINT64_C(10000000)
/* Integers above 2^53 are not all exactly representable as double. */
#define MAX_EXACT_DOUBLE_UINT64 UINT64_C(9007199254740992)

/* =========================================================================
 * Portable 64-bit PRNG (SplitMix64)
 * ========================================================================= */

void prng_seed(prng_t *rng, uint64_t seed) {
    if (rng) {
        rng->state = seed;
    }
}

uint64_t prng_next64(prng_t *rng) {
    uint64_t z = (rng->state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

/*
 * Generate an integer in [0, range - 1] without modulo bias.
 * Uses rejection sampling against the largest multiple of range.
 */
uint64_t prng_uniform_range(prng_t *rng, uint64_t range) {
    if (range <= 1) {
        return 0;
    }
    uint64_t threshold = (UINT64_MAX - range + 1) % range;
    while (1) {
        uint64_t r = prng_next64(rng);
        if (r >= threshold) {
            return r % range;
        }
    }
}

/*
 * Generate a uniform double in [0.0, 1.0) with 53 bits of precision.
 */
double prng_uniform_double(prng_t *rng) {
    return (double)(prng_next64(rng) >> 11) * (1.0 / 9007199254740992.0);
}

/* =========================================================================
 * Exact Discrete Zipfian Distribution Generator
 * ========================================================================= */

typedef struct zipf_table {
    uint64_t num_keys;
    double alpha;
    double *cdf;
} zipf_table_t;

static int zipf_table_init(zipf_table_t *zt, uint64_t num_keys, double alpha, char *err_msg, size_t err_len) {
    if (num_keys == 0 || num_keys > MAX_ZIPF_KEYS) {
        if (err_msg) {
            snprintf(err_msg, err_len, "Zipf key range must be between 1 and %" PRIu64, MAX_ZIPF_KEYS);
        }
        return -1;
    }
    if (alpha <= 0.0 || isnan(alpha) || isinf(alpha)) {
        if (err_msg) {
            snprintf(err_msg, err_len, "Zipf alpha parameter must be positive and finite");
        }
        return -1;
    }

    zt->num_keys = num_keys;
    zt->alpha = alpha;
    zt->cdf = malloc(num_keys * sizeof(double));
    if (!zt->cdf) {
        if (err_msg) {
            snprintf(err_msg, err_len, "Memory allocation failed for Zipf CDF table (%" PRIu64 " elements)", num_keys);
        }
        return -1;
    }

    double sum = 0.0;
    for (uint64_t i = 0; i < num_keys; i++) {
        double weight = pow((double)(i + 1), -alpha);
        sum += weight;
        zt->cdf[i] = sum;
    }

    if (sum <= 0.0 || isnan(sum) || isinf(sum)) {
        free(zt->cdf);
        zt->cdf = NULL;
        if (err_msg) {
            snprintf(err_msg, err_len, "Numerical overflow/underflow calculating harmonic sum");
        }
        return -1;
    }

    for (uint64_t i = 0; i < num_keys; i++) {
        zt->cdf[i] /= sum;
    }
    zt->cdf[num_keys - 1] = 1.0;

    return 0;
}

static void zipf_table_free(zipf_table_t *zt) {
    if (zt && zt->cdf) {
        free(zt->cdf);
        zt->cdf = NULL;
    }
}

static uint64_t zipf_sample(const zipf_table_t *zt, prng_t *rng) {
    double u = prng_uniform_double(rng);
    uint64_t low = 0;
    uint64_t high = zt->num_keys - 1;
    uint64_t result = zt->num_keys - 1;

    while (low <= high) {
        uint64_t mid = low + (high - low) / 2;
        if (zt->cdf[mid] > u) {
            result = mid;
            if (mid == 0) {
                break;
            }
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    return result;
}

/* =========================================================================
 * Configuration and Validation
 * ========================================================================= */

void trace_config_init_defaults(trace_config_t *cfg, workload_type_t type) {
    if (!cfg) return;
    cfg->workload = type;
    cfg->num_accesses = 10000;
    cfg->key_range = 1000;
    cfg->seed = 42;
    cfg->alpha = 1.0;
    cfg->hot_size = 50;
    cfg->hot_fraction = 0.5;
    cfg->output_file = NULL;
}

int trace_parse_workload_name(const char *name, workload_type_t *out_type) {
    if (!name || !out_type) return -1;
    if (strcmp(name, "uniform") == 0) {
        *out_type = WORKLOAD_UNIFORM;
        return 0;
    }
    if (strcmp(name, "zipf") == 0) {
        *out_type = WORKLOAD_ZIPF;
        return 0;
    }
    if (strcmp(name, "loop") == 0) {
        *out_type = WORKLOAD_LOOP;
        return 0;
    }
    if (strcmp(name, "scan") == 0) {
        *out_type = WORKLOAD_SCAN;
        return 0;
    }
    if (strcmp(name, "hot_scan") == 0) {
        *out_type = WORKLOAD_HOT_SCAN;
        return 0;
    }
    return -1;
}

const char *trace_workload_name(workload_type_t type) {
    switch (type) {
        case WORKLOAD_UNIFORM:  return "uniform";
        case WORKLOAD_ZIPF:     return "zipf";
        case WORKLOAD_LOOP:     return "loop";
        case WORKLOAD_SCAN:     return "scan";
        case WORKLOAD_HOT_SCAN: return "hot_scan";
        default:                return "unknown";
    }
}

/*
 * Compute the hot-phase access count and reject configurations that would
 * wrap uint64_t keys or lose integer precision in (n * hot_fraction).
 */
static int hot_scan_prepare(const trace_config_t *cfg, uint64_t *hot_accesses_out,
                            char *err_msg, size_t err_len) {
    if (cfg->hot_size == 0) {
        if (err_msg) snprintf(err_msg, err_len, "Hot set size must be greater than 0");
        return -1;
    }
    if (!(cfg->hot_fraction > 0.0 && cfg->hot_fraction < 1.0) ||
        isnan(cfg->hot_fraction) || isinf(cfg->hot_fraction)) {
        if (err_msg) snprintf(err_msg, err_len, "Hot fraction must be strictly between 0.0 and 1.0");
        return -1;
    }
    if (cfg->num_accesses > MAX_EXACT_DOUBLE_UINT64) {
        if (err_msg) {
            snprintf(err_msg, err_len,
                     "Number of accesses is too large for a precise hot-phase calculation");
        }
        return -1;
    }

    double product = (double)cfg->num_accesses * cfg->hot_fraction;
    if (isnan(product) || isinf(product) || product < 0.0) {
        if (err_msg) snprintf(err_msg, err_len, "Hot-phase access count overflowed");
        return -1;
    }

    uint64_t hot_accesses = (uint64_t)product;
    if (hot_accesses == 0 && cfg->num_accesses > 0) {
        hot_accesses = 1;
    }
    if (hot_accesses > cfg->num_accesses) {
        hot_accesses = cfg->num_accesses;
    }

    if (hot_accesses < cfg->num_accesses) {
        uint64_t scan_count = cfg->num_accesses - hot_accesses;
        /* Last scan key is hot_size + scan_count - 1. */
        if (scan_count > 0 && cfg->hot_size > UINT64_MAX - (scan_count - 1)) {
            if (err_msg) {
                snprintf(err_msg, err_len,
                         "hot_scan keys would overflow 64-bit range (hot_size + scan offset)");
            }
            return -1;
        }
    }

    if (hot_accesses_out) {
        *hot_accesses_out = hot_accesses;
    }
    return 0;
}

int trace_config_validate(const trace_config_t *cfg, char *err_msg, size_t err_len) {
    if (!cfg) {
        if (err_msg) snprintf(err_msg, err_len, "Configuration is NULL");
        return -1;
    }

    if (cfg->num_accesses == 0) {
        if (err_msg) snprintf(err_msg, err_len, "Number of accesses must be greater than 0");
        return -1;
    }

    switch (cfg->workload) {
        case WORKLOAD_UNIFORM:
        case WORKLOAD_LOOP:
            if (cfg->key_range == 0) {
                if (err_msg) snprintf(err_msg, err_len, "Key range must be greater than 0");
                return -1;
            }
            break;

        case WORKLOAD_ZIPF:
            if (cfg->key_range == 0 || cfg->key_range > MAX_ZIPF_KEYS) {
                if (err_msg) snprintf(err_msg, err_len, "Key range must be between 1 and %" PRIu64, MAX_ZIPF_KEYS);
                return -1;
            }
            if (cfg->alpha <= 0.0 || isnan(cfg->alpha) || isinf(cfg->alpha)) {
                if (err_msg) snprintf(err_msg, err_len, "Zipf alpha must be positive and finite");
                return -1;
            }
            break;

        case WORKLOAD_SCAN:
            /* Scan generates num_accesses distinct sequential keys, no extra requirements */
            break;

        case WORKLOAD_HOT_SCAN:
            return hot_scan_prepare(cfg, NULL, err_msg, err_len);

        default:
            if (err_msg) snprintf(err_msg, err_len, "Unknown workload type %d", (int)cfg->workload);
            return -1;
    }

    return 0;
}

/* =========================================================================
 * Generation Routines
 * ========================================================================= */

int trace_generate_stream(const trace_config_t *cfg, FILE *out) {
    if (!cfg || !out) {
        return -1;
    }

    char err_buf[256];
    if (trace_config_validate(cfg, err_buf, sizeof(err_buf)) != 0) {
        fprintf(stderr, "Configuration error: %s\n", err_buf);
        return -1;
    }

    prng_t rng;
    prng_seed(&rng, cfg->seed);

    zipf_table_t zt;
    bool has_zipf_table = false;

    if (cfg->workload == WORKLOAD_ZIPF) {
        if (zipf_table_init(&zt, cfg->key_range, cfg->alpha, err_buf, sizeof(err_buf)) != 0) {
            fprintf(stderr, "Zipf initialization failed: %s\n", err_buf);
            return -1;
        }
        has_zipf_table = true;
    }

    uint64_t hot_accesses = 0;
    if (cfg->workload == WORKLOAD_HOT_SCAN) {
        if (hot_scan_prepare(cfg, &hot_accesses, err_buf, sizeof(err_buf)) != 0) {
            if (has_zipf_table) {
                zipf_table_free(&zt);
            }
            fprintf(stderr, "Configuration error: %s\n", err_buf);
            return -1;
        }
    }

    for (uint64_t i = 0; i < cfg->num_accesses; i++) {
        uint64_t key = 0;

        switch (cfg->workload) {
            case WORKLOAD_UNIFORM:
                key = prng_uniform_range(&rng, cfg->key_range);
                break;

            case WORKLOAD_ZIPF:
                key = zipf_sample(&zt, &rng);
                break;

            case WORKLOAD_LOOP:
                key = i % cfg->key_range;
                break;

            case WORKLOAD_SCAN:
                key = i;
                break;

            case WORKLOAD_HOT_SCAN:
                if (i < hot_accesses) {
                    /* Phase 1: Accesses within hot set [0, hot_size - 1] */
                    key = prng_uniform_range(&rng, cfg->hot_size);
                } else {
                    /* Phase 2: Sequential scan over distinct keys starting at hot_size */
                    key = cfg->hot_size + (i - hot_accesses);
                }
                break;
        }

        if (fprintf(out, "%" PRIu64 "\n", key) < 0) {
            if (has_zipf_table) {
                zipf_table_free(&zt);
            }
            return -1;
        }
    }

    if (has_zipf_table) {
        zipf_table_free(&zt);
    }

    if (fflush(out) != 0) {
        return -1;
    }

    return 0;
}

int trace_generate_file(const trace_config_t *cfg, const char *path) {
    char err_buf[256];
    if (trace_config_validate(cfg, err_buf, sizeof(err_buf)) != 0) {
        fprintf(stderr, "Configuration error: %s\n", err_buf);
        return -1;
    }

    if (!path || strcmp(path, "-") == 0) {
        return trace_generate_stream(cfg, stdout);
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Error opening output file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    int rc = trace_generate_stream(cfg, fp);
    if (fclose(fp) != 0) {
        fprintf(stderr, "Error closing output file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    return rc;
}

int trace_generate_array(const trace_config_t *cfg, uint64_t *out_array, size_t count) {
    if (!cfg || !out_array) {
        return -1;
    }
    if (count < cfg->num_accesses) {
        return -1;
    }

    char err_buf[256];
    if (trace_config_validate(cfg, err_buf, sizeof(err_buf)) != 0) {
        return -1;
    }

    prng_t rng;
    prng_seed(&rng, cfg->seed);

    zipf_table_t zt;
    bool has_zipf_table = false;

    if (cfg->workload == WORKLOAD_ZIPF) {
        if (zipf_table_init(&zt, cfg->key_range, cfg->alpha, err_buf, sizeof(err_buf)) != 0) {
            return -1;
        }
        has_zipf_table = true;
    }

    uint64_t hot_accesses = 0;
    if (cfg->workload == WORKLOAD_HOT_SCAN) {
        if (hot_scan_prepare(cfg, &hot_accesses, err_buf, sizeof(err_buf)) != 0) {
            if (has_zipf_table) {
                zipf_table_free(&zt);
            }
            return -1;
        }
    }

    for (uint64_t i = 0; i < cfg->num_accesses; i++) {
        uint64_t key = 0;
        switch (cfg->workload) {
            case WORKLOAD_UNIFORM:
                key = prng_uniform_range(&rng, cfg->key_range);
                break;
            case WORKLOAD_ZIPF:
                key = zipf_sample(&zt, &rng);
                break;
            case WORKLOAD_LOOP:
                key = i % cfg->key_range;
                break;
            case WORKLOAD_SCAN:
                key = i;
                break;
            case WORKLOAD_HOT_SCAN:
                if (i < hot_accesses) {
                    key = prng_uniform_range(&rng, cfg->hot_size);
                } else {
                    key = cfg->hot_size + (i - hot_accesses);
                }
                break;
        }
        out_array[i] = key;
    }

    if (has_zipf_table) {
        zipf_table_free(&zt);
    }

    return 0;
}
