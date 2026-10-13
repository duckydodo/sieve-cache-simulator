#define _POSIX_C_SOURCE 199309L
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cache.h"
#include "benchmark.h"

#define MAX_LINE_LENGTH 128

static int parse_uint64(const char *s, uint64_t *out, bool require_positive) {
    if (!s) return -1;

    const char *p = s;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0' || *p == '-') {
        return -1;
    }

    char *endptr = NULL;
    errno = 0;
    unsigned long long val = strtoull(s, &endptr, 10);
    if (errno == ERANGE) return -1;
    if (endptr == s || *endptr != '\0') return -1;
#if ULLONG_MAX > UINT64_MAX
    if (val > UINT64_MAX) return -1;
#endif
    if (require_positive && val == 0) return -1;
    *out = (uint64_t)val;
    return 0;
}


int read_trace_keys(const char *filename, uint64_t **out_keys, size_t *out_count) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open trace file '%s': %s\n", filename, strerror(errno));
        return -1;
    }

    size_t capacity = 1024;
    uint64_t *keys = malloc(capacity * sizeof(uint64_t));
    if (!keys) {
        fclose(f);
        fprintf(stderr, "Error: Memory allocation failed for trace keys\n");
        return -1;
    }

    size_t count = 0;
    char line[MAX_LINE_LENGTH];
    size_t line_num = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;

        char *p = line;
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            continue;
        }

        char *end = p;
        while (*end != '\0' && !isspace((unsigned char)*end)) {
            end++;
        }
        *end = '\0';

        uint64_t key;
        if (parse_uint64(p, &key, false) != 0) {
            fprintf(stderr, "Error: Malformed key at line %zu in '%s'\n", line_num, filename);
            free(keys);
            fclose(f);
            return -1;
        }

        if (count >= capacity) {
            size_t new_capacity = capacity * 2;
            uint64_t *new_keys = realloc(keys, new_capacity * sizeof(uint64_t));
            if (!new_keys) {
                fprintf(stderr, "Error: Memory allocation failed for trace keys\n");
                free(keys);
                fclose(f);
                return -1;
            }
            keys = new_keys;
            capacity = new_capacity;
        }

        keys[count++] = key;
    }

    if (ferror(f)) {
        fprintf(stderr, "Error: Read error on trace file '%s'\n", filename);
        free(keys);
        fclose(f);
        return -1;
    }

    fclose(f);

    if (count == 0) {
        fprintf(stderr, "Error: Trace file '%s' is empty (no keys found)\n", filename);
        free(keys);
        return -1;
    }

    *out_keys = keys;
    *out_count = count;
    return 0;
}

int run_benchmark(FILE *out, const benchmark_config_t *cfg,
                  const uint64_t *keys, size_t key_count) {
    fprintf(out, "workload,trace,seed,accesses,capacity,policy,hits,misses,evictions,hit_ratio,miss_ratio,elapsed_ns\n");

    cache_policy_type_t policies[] = {
        CACHE_POLICY_FIFO,
        CACHE_POLICY_LRU,
        CACHE_POLICY_SIEVE
    };
    const int num_policies = 3;

    for (int p = 0; p < num_policies; p++) {
        cache_t *cache = cache_create(cfg->capacity, policies[p]);
        if (!cache) {
            fprintf(stderr, "Error: Failed to create cache with capacity %zu for policy %s\n",
                    cfg->capacity, cache_policy_name(policies[p]));
            return -1;
        }

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        for (size_t i = 0; i < key_count; i++) {
            cache_access(cache, keys[i]);
        }

        clock_gettime(CLOCK_MONOTONIC, &end);

        const cache_stats_t *stats = cache_get_stats(cache);

        uint64_t elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000ULL +
                              (end.tv_nsec - start.tv_nsec);

        double hit_ratio = stats->accesses > 0 ? (double)stats->hits / stats->accesses : 0.0;
        double miss_ratio = stats->accesses > 0 ? (double)stats->misses / stats->accesses : 0.0;

        fprintf(out, "%s,%s,%" PRIu64 ",%" PRIu64 ",%zu,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.6f,%.6f,%" PRIu64 "\n",
                cfg->workload ? cfg->workload : "unknown",
                cfg->trace_file,
                cfg->seed_provided ? cfg->seed : 0,
                stats->accesses,
                cfg->capacity,
                cache_policy_name(policies[p]),
                stats->hits,
                stats->misses,
                stats->evictions,
                hit_ratio,
                miss_ratio,
                elapsed_ns);

        cache_destroy(cache);
    }

    return 0;
}

#ifndef BENCHMARK_NO_MAIN
static void print_usage(const char *prog_name) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Replay a cache access trace through FIFO, LRU, and SIEVE policies and output metrics.\n"
        "\n"
        "Options:\n"
        "  -t, --trace <file>       Input trace file (one uint64_t key per line, required)\n"
        "  -c, --capacity <size>    Cache capacity (> 0, required)\n"
        "  -o, --output <file>      Output CSV file (default: stdout, '-' for stdout)\n"
        "  -w, --workload <name>    Workload name for metadata (optional)\n"
        "  -s, --seed <number>      Seed for metadata (optional)\n"
        "  -h, --help               Show this help message and exit\n"
        "\n"
        "CSV columns:\n"
        "  workload,trace,seed,accesses,capacity,policy,hits,misses,evictions,hit_ratio,miss_ratio,elapsed_ns\n"
        "\n"
        "Examples:\n"
        "  %s -t traces/uniform.txt -c 64 -o results/uniform.csv\n"
        "  %s -t traces/zipf.txt -c 128 -w zipf -s 42 -o results/zipf.csv\n",
        prog_name, prog_name, prog_name
    );
}

int main(int argc, char **argv) {
    benchmark_config_t cfg = {
        .trace_file = NULL,
        .output_file = NULL,
        .capacity = 0,
        .workload = NULL,
        .seed = 0,
        .seed_provided = false
    };

    static struct option long_options[] = {
        {"trace",    required_argument, 0, 't'},
        {"capacity", required_argument, 0, 'c'},
        {"output",   required_argument, 0, 'o'},
        {"workload", required_argument, 0, 'w'},
        {"seed",     required_argument, 0, 's'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int opt_index = 0;

    while ((opt = getopt_long(argc, argv, "t:c:o:w:s:h", long_options, &opt_index)) != -1) {
        switch (opt) {
            case 't':
                cfg.trace_file = optarg;
                break;

            case 'c':
                if (parse_uint64(optarg, &cfg.capacity, true) != 0) {
                    fprintf(stderr, "Error: Invalid capacity '%s'. Must be a positive integer.\n", optarg);
                    return 1;
                }
                break;

            case 'o':
                cfg.output_file = optarg;
                break;

            case 'w':
                cfg.workload = optarg;
                break;

            case 's':
                if (parse_uint64(optarg, &cfg.seed, false) != 0) {
                    fprintf(stderr, "Error: Invalid seed '%s'. Must be a non-negative integer.\n", optarg);
                    return 1;
                }
                cfg.seed_provided = true;
                break;

            case 'h':
                print_usage(argv[0]);
                return 0;

            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!cfg.trace_file) {
        fprintf(stderr, "Error: --trace is required\n");
        print_usage(argv[0]);
        return 1;
    }

    if (cfg.capacity == 0) {
        fprintf(stderr, "Error: --capacity is required and must be > 0\n");
        print_usage(argv[0]);
        return 1;
    }

    uint64_t *keys = NULL;
    size_t key_count = 0;

    if (read_trace_keys(cfg.trace_file, &keys, &key_count) != 0) {
        return 1;
    }

    FILE *out = stdout;
    if (cfg.output_file && strcmp(cfg.output_file, "-") != 0) {
        out = fopen(cfg.output_file, "w");
        if (!out) {
            fprintf(stderr, "Error: Cannot open output file '%s': %s\n", cfg.output_file, strerror(errno));
            free(keys);
            return 1;
        }
    }

    int result = run_benchmark(out, &cfg, keys, key_count);

    free(keys);

    if (out != stdout) {
        fclose(out);
    }

    if (result == 0 && cfg.output_file && strcmp(cfg.output_file, "-") != 0) {
        fprintf(stderr, "Benchmark completed: trace '%s', capacity %zu, output '%s'\n",
                cfg.trace_file, cfg.capacity, cfg.output_file);
    }

    return result;
}
#endif /* BENCHMARK_NO_MAIN */
