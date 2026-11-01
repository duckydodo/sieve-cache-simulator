#define _POSIX_C_SOURCE 200809L
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
    unsigned long long val = strtoull(p, &endptr, 10);
    if (errno == ERANGE) return -1;
    if (endptr == p || *endptr != '\0') return -1;
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

        /* Skip leading whitespace */
        char *p = line;
        while (isspace((unsigned char)*p)) {
            p++;
        }
        /* Skip blank / whitespace-only lines */
        if (*p == '\0') {
            continue;
        }

        /*
         * Locate the end of the first token.  Everything from here to end-of-
         * line must be whitespace; any non-whitespace after the token is an
         * extra token (e.g. "1 garbage" or "1 2") and must be rejected.
         */
        char *tok_end = p;
        while (*tok_end != '\0' && !isspace((unsigned char)*tok_end)) {
            tok_end++;
        }

        /* Check for trailing garbage after the token */
        char *after = tok_end;
        while (isspace((unsigned char)*after)) {
            after++;
        }
        if (*after != '\0') {
            fprintf(stderr,
                "Error: Malformed key at line %zu in '%s' (trailing garbage after key)\n",
                line_num, filename);
            free(keys);
            fclose(f);
            return -1;
        }

        /* NUL-terminate the token so parse_uint64 sees exactly the key */
        *tok_end = '\0';

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

static int cmp_uint64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

size_t count_distinct_keys(const uint64_t *keys, size_t count) {
    if (count == 0) return 0;
    if (count == 1) return 1;

    uint64_t *sorted = malloc(count * sizeof(uint64_t));
    if (!sorted) {
        /* If memory allocation fails, return 0 as best-effort indicator */
        return 0;
    }

    memcpy(sorted, keys, count * sizeof(uint64_t));
    qsort(sorted, count, sizeof(uint64_t), cmp_uint64);

    size_t distinct = 1;
    for (size_t i = 1; i < count; i++) {
        if (sorted[i] != sorted[i - 1]) {
            distinct++;
        }
    }

    free(sorted);
    return distinct;
}

static void write_csv_escaped(FILE *out, const char *s) {
    if (!s) {
        return;
    }
    bool needs_quotes = false;
    for (const char *p = s; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        fputs(s, out);
        return;
    }

    fputc('"', out);
    for (const char *p = s; *p; p++) {
        if (*p == '"') {
            fputc('"', out); /* RFC 4180: double the quote */
        }
        fputc(*p, out);
    }
    fputc('"', out);
}

int run_benchmark(FILE *out, const benchmark_config_t *cfg,
                  const uint64_t *keys, size_t key_count) {
    if (!out || !cfg || (!keys && key_count > 0)) {
        return -1;
    }

    /* Determine the list of capacities to run */
    size_t single_cap = cfg->capacity;
    const size_t *caps = cfg->capacities;
    size_t num_caps = cfg->num_capacities;

    if (!caps || num_caps == 0) {
        if (single_cap > 0) {
            caps = &single_cap;
            num_caps = 1;
        } else {
            fprintf(stderr, "Error: Capacity must be greater than 0\n");
            return -1;
        }
    }

    /* Compute distinct keys once across the trace */
    size_t distinct_keys = count_distinct_keys(keys, key_count);

    /* Output standardized 13-column CSV header */
    fprintf(out, "workload,trace,seed,accesses,distinct_keys,capacity,policy,hits,misses,evictions,hit_ratio,miss_ratio,elapsed_ns\n");

    cache_policy_type_t policies[] = {
        CACHE_POLICY_FIFO,
        CACHE_POLICY_LRU,
        CACHE_POLICY_SIEVE
    };
    const int num_policies = 3;

    for (size_t c = 0; c < num_caps; c++) {
        size_t cap = caps[c];
        if (cap == 0) {
            fprintf(stderr, "Error: Capacity in list is 0 (must be > 0)\n");
            return -1;
        }

        for (int p = 0; p < num_policies; p++) {
            cache_t *cache = cache_create(cap, policies[p]);
            if (!cache) {
                fprintf(stderr, "Error: Failed to create cache with capacity %zu for policy %s\n",
                        cap, cache_policy_name(policies[p]));
                return -1;
            }

            /*
             * Replay timing: tightly measure cache_access loop only.
             * Excludes trace loading, memory allocations, cache creation,
             * and CSV writing.
             */
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

            write_csv_escaped(out, cfg->workload ? cfg->workload : "unknown");
            fputc(',', out);
            write_csv_escaped(out, cfg->trace_file ? cfg->trace_file : "unknown");
            fprintf(out, ",%" PRIu64 ",%" PRIu64 ",%zu,%zu,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.6f,%.6f,%" PRIu64 "\n",
                    cfg->seed_provided ? cfg->seed : 0,
                    stats->accesses,
                    distinct_keys,
                    cap,
                    cache_policy_name(policies[p]),
                    stats->hits,
                    stats->misses,
                    stats->evictions,
                    hit_ratio,
                    miss_ratio,
                    elapsed_ns);

            cache_destroy(cache);
        }
    }

    return 0;
}

#ifndef BENCHMARK_NO_MAIN
static int parse_capacities_list(const char *arg, size_t **out_caps, size_t *out_count) {
    if (!arg || *arg == '\0') return -1;
    char *dup = strdup(arg);
    if (!dup) return -1;

    size_t cap_allocated = 4;
    size_t *caps = malloc(cap_allocated * sizeof(size_t));
    if (!caps) {
        free(dup);
        return -1;
    }
    size_t count = 0;

    char *saveptr = NULL;
    char *token = strtok_r(dup, ",", &saveptr);
    while (token != NULL) {
        while (isspace((unsigned char)*token)) token++;
        char *end = token + strlen(token);
        while (end > token && isspace((unsigned char)*(end - 1))) {
            *(--end) = '\0';
        }
        if (*token == '\0') {
            free(dup);
            free(caps);
            return -1;
        }

        uint64_t val;
        if (parse_uint64(token, &val, true) != 0 || val == 0 || val > SIZE_MAX) {
            free(dup);
            free(caps);
            return -1;
        }

        if (count >= cap_allocated) {
            size_t new_alloc = cap_allocated * 2;
            size_t *new_caps = realloc(caps, new_alloc * sizeof(size_t));
            if (!new_caps) {
                free(dup);
                free(caps);
                return -1;
            }
            caps = new_caps;
            cap_allocated = new_alloc;
        }

        caps[count++] = (size_t)val;
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(dup);
    if (count == 0) {
        free(caps);
        return -1;
    }

    *out_caps = caps;
    *out_count = count;
    return 0;
}

static void print_usage(const char *prog_name) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Replay a cache access trace through FIFO, LRU, and SIEVE policies and output metrics.\n"
        "\n"
        "Options:\n"
        "  -t, --trace <file>         Input trace file (one uint64_t key per line, required)\n"
        "  -c, --capacity <size>      Cache capacity (> 0, required; supports comma-separated list)\n"
        "  -C, --capacities <list>    List of capacities (e.g. 16,32,64,128)\n"
        "  -o, --output <file>        Output CSV file (default: stdout, '-' for stdout)\n"
        "  -w, --workload <name>      Workload name for metadata (optional)\n"
        "  -s, --seed <number>        Seed for metadata (optional)\n"
        "  -h, --help                 Show this help message and exit\n"
        "\n"
        "CSV columns:\n"
        "  workload,trace,seed,accesses,distinct_keys,capacity,policy,hits,misses,evictions,hit_ratio,miss_ratio,elapsed_ns\n"
        "\n"
        "Examples:\n"
        "  %s -t traces/uniform.txt -c 64 -o results/uniform.csv\n"
        "  %s -t traces/zipf.txt --capacities 16,32,64,128 -w zipf -s 42 -o results/zipf.csv\n",
        prog_name, prog_name, prog_name
    );
}

int main(int argc, char **argv) {
    benchmark_config_t cfg = {
        .trace_file = NULL,
        .output_file = NULL,
        .capacity = 0,
        .capacities = NULL,
        .num_capacities = 0,
        .workload = NULL,
        .seed = 0,
        .seed_provided = false
    };

    static struct option long_options[] = {
        {"trace",      required_argument, 0, 't'},
        {"capacity",   required_argument, 0, 'c'},
        {"capacities", required_argument, 0, 'C'},
        {"output",     required_argument, 0, 'o'},
        {"workload",   required_argument, 0, 'w'},
        {"seed",       required_argument, 0, 's'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    size_t *parsed_caps = NULL;
    size_t num_parsed_caps = 0;

    int opt;
    int opt_index = 0;

    while ((opt = getopt_long(argc, argv, "t:c:C:o:w:s:h", long_options, &opt_index)) != -1) {
        switch (opt) {
            case 't':
                cfg.trace_file = optarg;
                break;

            case 'c':
            case 'C': {
                size_t *caps = NULL;
                size_t count = 0;
                if (parse_capacities_list(optarg, &caps, &count) != 0) {
                    fprintf(stderr, "Error: Invalid capacity '%s'. Must be positive integer(s).\n", optarg);
                    free(parsed_caps);
                    return 1;
                }
                free(parsed_caps);
                parsed_caps = caps;
                num_parsed_caps = count;
                cfg.capacity = caps[0];
                cfg.capacities = parsed_caps;
                cfg.num_capacities = num_parsed_caps;
                break;
            }

            case 'o':
                cfg.output_file = optarg;
                break;

            case 'w':
                cfg.workload = optarg;
                break;

            case 's': {
                uint64_t seed_val;
                if (parse_uint64(optarg, &seed_val, false) != 0) {
                    fprintf(stderr, "Error: Invalid seed '%s'. Must be a non-negative integer.\n", optarg);
                    free(parsed_caps);
                    return 1;
                }
                cfg.seed = seed_val;
                cfg.seed_provided = true;
                break;
            }

            case 'h':
                print_usage(argv[0]);
                free(parsed_caps);
                return 0;

            default:
                print_usage(argv[0]);
                free(parsed_caps);
                return 1;
        }
    }

    if (!cfg.trace_file) {
        fprintf(stderr, "Error: --trace is required\n");
        print_usage(argv[0]);
        free(parsed_caps);
        return 1;
    }

    if (cfg.num_capacities == 0 && cfg.capacity == 0) {
        fprintf(stderr, "Error: --capacity is required and must be > 0\n");
        print_usage(argv[0]);
        free(parsed_caps);
        return 1;
    }

    uint64_t *keys = NULL;
    size_t key_count = 0;

    if (read_trace_keys(cfg.trace_file, &keys, &key_count) != 0) {
        free(parsed_caps);
        return 1;
    }

    FILE *out = stdout;
    if (cfg.output_file && strcmp(cfg.output_file, "-") != 0) {
        out = fopen(cfg.output_file, "w");
        if (!out) {
            fprintf(stderr, "Error: Cannot open output file '%s': %s\n", cfg.output_file, strerror(errno));
            free(keys);
            free(parsed_caps);
            return 1;
        }
    }

    int result = run_benchmark(out, &cfg, keys, key_count);

    free(keys);

    if (out != stdout) {
        fclose(out);
    }

    if (result == 0 && cfg.output_file && strcmp(cfg.output_file, "-") != 0) {
        fprintf(stderr, "Benchmark completed: trace '%s', %zu capacities, output '%s'\n",
                cfg.trace_file, cfg.num_capacities ? cfg.num_capacities : 1, cfg.output_file);
    }

    free(parsed_caps);
    return result;
}
#endif /* BENCHMARK_NO_MAIN */
