#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tracegen.h"

/*
 * Parse an unsigned 64-bit integer from a C string.
 * Returns 0 on success, -1 on:
 *   - empty string, NULL, or whitespace-only input
 *   - a minus sign (strtoull would wrap negatives)
 *   - trailing non-digit characters
 *   - overflow (errno == ERANGE, or value above UINT64_MAX)
 *   - value is 0 when require_positive is true
 */
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

/*
 * Parse a finite double from a C string.
 * Returns 0 on success, -1 on:
 *   - empty string, trailing junk, conversion failure, or ERANGE
 *   - NaN or infinity
 *   - value <= 0 when require_positive is true
 *   - value outside (lo_exclusive, hi_exclusive) when those bounds differ
 */
static int parse_double(const char *s, double *out,
                        bool require_positive,
                        double lo_exclusive, double hi_exclusive) {
    if (!s || s[0] == '\0') return -1;
    char *endptr = NULL;
    errno = 0;
    double val = strtod(s, &endptr);
    if (errno == ERANGE) return -1;
    if (endptr == s || *endptr != '\0') return -1;
    if (isnan(val) || isinf(val)) return -1;
    if (require_positive && !(val > 0.0)) return -1;
    if (lo_exclusive != hi_exclusive) {
        if (!(val > lo_exclusive) || !(val < hi_exclusive)) return -1;
    }
    *out = val;
    return 0;
}

static void print_usage(const char *prog_name) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Generate synthetic cache access traces with 64-bit integer keys (one key per line).\n"
        "\n"
        "Options:\n"
        "  -w, --workload <type>     Workload type: uniform, zipf, loop, scan, hot_scan\n"
        "                            (default: uniform)\n"
        "  -n, --accesses <count>    Number of access requests to generate (> 0, default: 10000)\n"
        "  -k, --keys <count>        Key range or working set size (> 0, default: 1000)\n"
        "  -s, --seed <number>       Pseudo-random generator seed (default: 42)\n"
        "  -a, --alpha <float>       Zipf skew parameter alpha (> 0.0, default: 1.0)\n"
        "      --hot-size <count>    Hot set size for hot_scan workload (default: 50)\n"
        "      --hot-frac <float>    Fraction of requests in hot phase (0.0 < frac < 1.0, default: 0.5)\n"
        "  -o, --output <file>       Output file path (default: stdout, '-' for stdout)\n"
        "  -h, --help                Show this help message and exit\n"
        "\n"
        "Workloads:\n"
        "  uniform   : Random accesses across [0, keys-1] without modulo bias\n"
        "  zipf      : Skewed accesses following exact discrete Zipf(alpha) distribution\n"
        "  loop      : Repeated sequential cycling through working set 0, 1, ..., keys-1\n"
        "  scan      : Sequential one-pass access across 0, 1, ..., accesses-1\n"
        "  hot_scan  : Repeated accesses to hot set [0, hot_size-1] followed by sequential scan\n"
        "\n"
        "Examples:\n"
        "  %s -w uniform -n 50000 -k 5000 -s 12345 -o traces/uniform.txt\n"
        "  %s -w zipf -n 100000 -k 10000 -a 1.0 -s 42 -o traces/zipf.txt\n"
        "  %s -w loop -n 20000 -k 500 -o traces/loop.txt\n"
        "  %s -w scan -n 50000 -o traces/scan.txt\n"
        "  %s -w hot_scan -n 50000 --hot-size 100 --hot-frac 0.6 -o traces/hot_scan.txt\n",
        prog_name, prog_name, prog_name, prog_name, prog_name, prog_name
    );
}

int main(int argc, char **argv) {
    trace_config_t config;
    trace_config_init_defaults(&config, WORKLOAD_UNIFORM);

    static struct option long_options[] = {
        {"workload",  required_argument, 0, 'w'},
        {"accesses",  required_argument, 0, 'n'},
        {"keys",      required_argument, 0, 'k'},
        {"seed",      required_argument, 0, 's'},
        {"alpha",     required_argument, 0, 'a'},
        {"hot-size",  required_argument, 0, 1001},
        {"hot-frac",  required_argument, 0, 1002},
        {"output",    required_argument, 0, 'o'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int opt_index = 0;

    while ((opt = getopt_long(argc, argv, "w:n:k:s:a:o:h", long_options, &opt_index)) != -1) {
        switch (opt) {
            case 'w':
                if (trace_parse_workload_name(optarg, &config.workload) != 0) {
                    fprintf(stderr, "Error: Unknown workload '%s'. Expected: uniform, zipf, loop, scan, hot_scan\n", optarg);
                    return 1;
                }
                break;

            case 'n':
                if (parse_uint64(optarg, &config.num_accesses, true) != 0) {
                    fprintf(stderr, "Error: Invalid access count '%s'. Must be a positive integer (no leading '-', no overflow).\n", optarg);
                    return 1;
                }
                break;

            case 'k':
                if (parse_uint64(optarg, &config.key_range, true) != 0) {
                    fprintf(stderr, "Error: Invalid key range '%s'. Must be a positive integer (no leading '-', no overflow).\n", optarg);
                    return 1;
                }
                break;

            case 's':
                if (parse_uint64(optarg, &config.seed, false) != 0) {
                    fprintf(stderr, "Error: Invalid seed '%s'. Must be a non-negative integer (no leading '-', no overflow).\n", optarg);
                    return 1;
                }
                break;

            case 'a':
                /* alpha must be finite and > 0; no bounded upper limit — pass lo==hi==0 to skip range check */
                if (parse_double(optarg, &config.alpha, true, 0.0, 0.0) != 0) {
                    fprintf(stderr, "Error: Invalid Zipf alpha '%s'. Must be a finite positive number.\n", optarg);
                    return 1;
                }
                break;

            case 1001: /* --hot-size */
                if (parse_uint64(optarg, &config.hot_size, true) != 0) {
                    fprintf(stderr, "Error: Invalid hot set size '%s'. Must be a positive integer (no leading '-', no overflow).\n", optarg);
                    return 1;
                }
                break;

            case 1002: /* --hot-frac */
                /* hot_fraction must be strictly in (0.0, 1.0) */
                if (parse_double(optarg, &config.hot_fraction, false, 0.0, 1.0) != 0) {
                    fprintf(stderr, "Error: Invalid hot fraction '%s'. Must be a finite value strictly between 0.0 and 1.0.\n", optarg);
                    return 1;
                }
                break;

            case 'o':
                config.output_file = optarg;
                break;

            case 'h':
                print_usage(argv[0]);
                return 0;

            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    char err_buf[256];
    if (trace_config_validate(&config, err_buf, sizeof(err_buf)) != 0) {
        fprintf(stderr, "Error: %s\n", err_buf);
        return 1;
    }

    if (trace_generate_file(&config, config.output_file) != 0) {
        return 1;
    }

    if (config.output_file && strcmp(config.output_file, "-") != 0) {
        fprintf(stderr, "Generated %" PRIu64 " accesses (workload: %s) to '%s'\n",
                config.num_accesses, trace_workload_name(config.workload), config.output_file);
    }

    return 0;
}
