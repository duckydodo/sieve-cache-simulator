#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "benchmark.h"
#include "cache.h"

#define TEST_TEMP_DIR "test_benchmark_temp"
#define TEST_TRACE_FILE TEST_TEMP_DIR "/trace.txt"
#define TEST_OUTPUT_FILE TEST_TEMP_DIR "/output.csv"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
    } else { \
        fprintf(stderr, "FAIL: %s (at %s:%d)\n", msg, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

static void setup_test_dir(void) {
    mkdir(TEST_TEMP_DIR, 0755);
}

static void cleanup_test_dir(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_TEMP_DIR);
    system(cmd);
}

static void write_trace_file(const char *content) {
    FILE *f = fopen(TEST_TRACE_FILE, "w");
    TEST_ASSERT(f != NULL, "Trace file opened for writing");
    fprintf(f, "%s", content);
    fclose(f);
}



static void read_csv_lines(char ***out_lines, int *out_count) {
    FILE *f = fopen(TEST_OUTPUT_FILE, "r");
    TEST_ASSERT(f != NULL, "CSV output file opened");

    char **lines = NULL;
    int count = 0;
    char line[1024];

    while (fgets(line, sizeof(line), f)) {
        lines = realloc(lines, (count + 1) * sizeof(char *));
        TEST_ASSERT(lines != NULL, "CSV lines allocation");
        lines[count] = malloc(strlen(line) + 1);
        TEST_ASSERT(lines[count] != NULL, "CSV line allocation");
        strcpy(lines[count], line);
        count++;
    }

    fclose(f);
    *out_lines = lines;
    *out_count = count;
}

static void free_csv_lines(char **lines, int count) {
    for (int i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
}

static int run_benchmark_wrapper(const char *trace_file, size_t capacity, const char *output_file,
                                  const char *workload, const char *seed) {
    benchmark_config_t cfg = {
        .trace_file = trace_file,
        .output_file = output_file,
        .capacity = capacity,
        .workload = workload,
        .seed = 0,
        .seed_provided = false
    };

    if (seed) {
        unsigned long long seed_val;
        if (sscanf(seed, "%llu", &seed_val) == 1) {
            cfg.seed = (uint64_t)seed_val;
            cfg.seed_provided = true;
        }
    }

    uint64_t *keys = NULL;
    size_t key_count = 0;

    if (read_trace_keys(trace_file, &keys, &key_count) != 0) {
        return -1;
    }

    FILE *out = fopen(output_file, "w");
    if (!out) {
        free(keys);
        return -1;
    }

    int result = run_benchmark(out, &cfg, keys, key_count);

    free(keys);
    fclose(out);

    return result;
}

static void parse_csv_field(const char *line, int field_index, char *out_field, size_t out_size) {
    int current_field = 0;
    size_t pos = 0;
    bool in_quotes = false;

    for (size_t i = 0; line[i] != '\0' && line[i] != '\n'; i++) {
        if (line[i] == '"') {
            in_quotes = !in_quotes;
        } else if (line[i] == ',' && !in_quotes) {
            if (current_field == field_index) {
                break; /* target field is complete; stop before pos is reset */
            }
            current_field++;
            pos = 0;
        } else {
            if (current_field == field_index && pos < out_size - 1) {
                out_field[pos++] = line[i];
            }
        }
    }
    out_field[pos] = '\0';
}

static uint64_t parse_csv_uint64(const char *line, int field_index) {
    char field[64];
    parse_csv_field(line, field_index, field, sizeof(field));
    return strtoull(field, NULL, 10);
}

static double parse_csv_double(const char *line, int field_index) {
    char field[64];
    parse_csv_field(line, field_index, field, sizeof(field));
    return strtod(field, NULL);
}

static void test_tiny_known_trace(void) {
    write_trace_file("1\n2\n3\n1\n2\n3\n");

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 3, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result == 0, "Benchmark succeeded on tiny trace");

    char **lines;
    int line_count;
    read_csv_lines(&lines, &line_count);

    TEST_ASSERT(line_count == 4, "CSV has header + 3 policy rows");
    TEST_ASSERT(strstr(lines[0], "workload,trace") != NULL, "CSV header is correct");

    for (int i = 1; i < 4; i++) {
        uint64_t accesses = parse_csv_uint64(lines[i], 3);
        uint64_t hits = parse_csv_uint64(lines[i], 6);
        uint64_t misses = parse_csv_uint64(lines[i], 7);
        TEST_ASSERT(accesses == 6, "Access count is 6");
        TEST_ASSERT(hits + misses == 6, "Hits + misses == accesses");
        TEST_ASSERT(misses <= 6, "Misses <= accesses");
    }

    free_csv_lines(lines, line_count);
}

static void test_repeated_keys(void) {
    write_trace_file("100\n100\n100\n100\n100\n");

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 5, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result == 0, "Benchmark succeeded on repeated keys");

    char **lines;
    int line_count;
    read_csv_lines(&lines, &line_count);

    for (int i = 1; i < 4; i++) {
        uint64_t hits = parse_csv_uint64(lines[i], 6);
        uint64_t misses = parse_csv_uint64(lines[i], 7);
        TEST_ASSERT(hits == 4, "4 hits (1 miss, then 4 hits)");
        TEST_ASSERT(misses == 1, "1 miss");
    }

    free_csv_lines(lines, line_count);
}

static void test_capacity_one(void) {
    write_trace_file("10\n20\n30\n");

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 1, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result == 0, "Benchmark succeeded with capacity 1");

    char **lines;
    int line_count;
    read_csv_lines(&lines, &line_count);

    for (int i = 1; i < 4; i++) {
        uint64_t accesses = parse_csv_uint64(lines[i], 3);
        uint64_t hits = parse_csv_uint64(lines[i], 6);
        uint64_t misses = parse_csv_uint64(lines[i], 7);
        uint64_t evictions = parse_csv_uint64(lines[i], 8);
        TEST_ASSERT(accesses == 3, "3 accesses");
        TEST_ASSERT(hits == 0, "0 hits");
        TEST_ASSERT(misses == 3, "3 misses");
        TEST_ASSERT(evictions == 2, "2 evictions");
    }

    free_csv_lines(lines, line_count);
}

static void test_key_zero(void) {
    write_trace_file("0\n1\n0\n1\n");

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 2, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result == 0, "Benchmark succeeded with key 0");

    char **lines;
    int line_count;
    read_csv_lines(&lines, &line_count);

    for (int i = 1; i < 4; i++) {
        uint64_t hits = parse_csv_uint64(lines[i], 6);
        uint64_t misses = parse_csv_uint64(lines[i], 7);
        TEST_ASSERT(hits == 2, "2 hits");
        TEST_ASSERT(misses == 2, "2 misses");
    }

    free_csv_lines(lines, line_count);
}

static void test_key_uint64_max(void) {
    char line[64];
    snprintf(line, sizeof(line), "18446744073709551615\n0\n18446744073709551615\n");
    write_trace_file(line);

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 2, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result == 0, "Benchmark succeeded with UINT64_MAX");

    char **csv_lines;
    int line_count;
    read_csv_lines(&csv_lines, &line_count);

    for (int i = 1; i < 4; i++) {
        uint64_t hits = parse_csv_uint64(csv_lines[i], 6);
        uint64_t misses = parse_csv_uint64(csv_lines[i], 7);
        TEST_ASSERT(hits == 1, "1 hit");
        TEST_ASSERT(misses == 2, "2 misses");
    }

    free_csv_lines(csv_lines, line_count);
}

static void test_empty_trace(void) {
    write_trace_file("");

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 10, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result != 0, "Benchmark fails on empty trace");
}

static void test_malformed_input(void) {
    write_trace_file("1\nabc\n3\n");

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 10, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result != 0, "Benchmark fails on malformed input");
}

static void test_missing_trace_file(void) {
    int result = run_benchmark_wrapper("nonexistent.txt", 10, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result != 0, "Benchmark fails on missing trace file");
}

static void test_invalid_capacity(void) {
    write_trace_file("1\n2\n3\n");

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 0, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result != 0, "Benchmark fails on invalid capacity");
}

static void test_csv_header_and_row_count(void) {
    write_trace_file("1\n2\n3\n");

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 10, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result == 0, "Benchmark succeeded");

    char **lines;
    int line_count;
    read_csv_lines(&lines, &line_count);

    TEST_ASSERT(line_count == 4, "CSV has header + 3 policy rows");

    char header[256];
    strncpy(header, lines[0], sizeof(header) - 1);
    TEST_ASSERT(strstr(header, "workload") != NULL, "Header contains 'workload'");
    TEST_ASSERT(strstr(header, "trace") != NULL, "Header contains 'trace'");
    TEST_ASSERT(strstr(header, "seed") != NULL, "Header contains 'seed'");
    TEST_ASSERT(strstr(header, "accesses") != NULL, "Header contains 'accesses'");
    TEST_ASSERT(strstr(header, "capacity") != NULL, "Header contains 'capacity'");
    TEST_ASSERT(strstr(header, "policy") != NULL, "Header contains 'policy'");
    TEST_ASSERT(strstr(header, "hits") != NULL, "Header contains 'hits'");
    TEST_ASSERT(strstr(header, "misses") != NULL, "Header contains 'misses'");
    TEST_ASSERT(strstr(header, "evictions") != NULL, "Header contains 'evictions'");
    TEST_ASSERT(strstr(header, "hit_ratio") != NULL, "Header contains 'hit_ratio'");
    TEST_ASSERT(strstr(header, "miss_ratio") != NULL, "Header contains 'miss_ratio'");
    TEST_ASSERT(strstr(header, "elapsed_ns") != NULL, "Header contains 'elapsed_ns'");

    free_csv_lines(lines, line_count);
}

static void test_metric_invariants(void) {
    write_trace_file("1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n");

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 5, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result == 0, "Benchmark succeeded");

    char **lines;
    int line_count;
    read_csv_lines(&lines, &line_count);

    for (int i = 1; i < 4; i++) {
        uint64_t accesses = parse_csv_uint64(lines[i], 3);
        uint64_t hits = parse_csv_uint64(lines[i], 6);
        uint64_t misses = parse_csv_uint64(lines[i], 7);
        uint64_t evictions = parse_csv_uint64(lines[i], 8);
        double hit_ratio = parse_csv_double(lines[i], 9);
        double miss_ratio = parse_csv_double(lines[i], 10);

        TEST_ASSERT(hits + misses == accesses, "hits + misses == accesses");
        TEST_ASSERT(misses <= accesses, "misses <= accesses");
        TEST_ASSERT(evictions <= misses, "evictions <= misses");
        TEST_ASSERT(hit_ratio >= 0.0 && hit_ratio <= 1.0, "hit_ratio in [0, 1]");
        TEST_ASSERT(miss_ratio >= 0.0 && miss_ratio <= 1.0, "miss_ratio in [0, 1]");
        TEST_ASSERT(hit_ratio + miss_ratio >= 0.999 && hit_ratio + miss_ratio <= 1.001,
                    "hit_ratio + miss_ratio ≈ 1.0");
    }

    free_csv_lines(lines, line_count);
}

static void test_deterministic_replay(void) {
    write_trace_file("1\n2\n3\n4\n5\n");

    int result1 = run_benchmark_wrapper(TEST_TRACE_FILE, 3, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result1 == 0, "First benchmark run succeeded");

    char **lines1;
    int line_count1;
    read_csv_lines(&lines1, &line_count1);

    char output2[256];
    snprintf(output2, sizeof(output2), "%s/output2.csv", TEST_TEMP_DIR);
    int result2 = run_benchmark_wrapper(TEST_TRACE_FILE, 3, output2, NULL, NULL);
    TEST_ASSERT(result2 == 0, "Second benchmark run succeeded");

    FILE *f2 = fopen(output2, "r");
    TEST_ASSERT(f2 != NULL, "Second output file opened");
    char line2[1024];
    int row = 0;
    while (fgets(line2, sizeof(line2), f2) && row < line_count1) {
        char *line1 = lines1[row];
        char policy1[32], policy2[32];
        parse_csv_field(line1, 5, policy1, sizeof(policy1));
        parse_csv_field(line2, 5, policy2, sizeof(policy2));

        if (strcmp(policy1, policy2) == 0) {
            uint64_t hits1 = parse_csv_uint64(line1, 6);
            uint64_t hits2 = parse_csv_uint64(line2, 6);
            uint64_t misses1 = parse_csv_uint64(line1, 7);
            uint64_t misses2 = parse_csv_uint64(line2, 7);
            uint64_t evictions1 = parse_csv_uint64(line1, 8);
            uint64_t evictions2 = parse_csv_uint64(line2, 8);

            TEST_ASSERT(hits1 == hits2, "Hits are deterministic");
            TEST_ASSERT(misses1 == misses2, "Misses are deterministic");
            TEST_ASSERT(evictions1 == evictions2, "Evictions are deterministic");
        }
        row++;
    }
    fclose(f2);

    free_csv_lines(lines1, line_count1);
    unlink(output2);
}

static void test_all_policies_on_same_trace(void) {
    write_trace_file("1\n2\n3\n4\n5\n");

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 3, TEST_OUTPUT_FILE, NULL, NULL);
    TEST_ASSERT(result == 0, "Benchmark succeeded");

    char **lines;
    int line_count;
    read_csv_lines(&lines, &line_count);

    TEST_ASSERT(line_count == 4, "CSV has 4 rows (header + 3 policies)");

    char policies[3][32];
    for (int i = 0; i < 3; i++) {
        parse_csv_field(lines[i + 1], 5, policies[i], sizeof(policies[i]));
    }

    bool has_fifo = false, has_lru = false, has_sieve = false;
    for (int i = 0; i < 3; i++) {
        if (strcmp(policies[i], "FIFO") == 0) has_fifo = true;
        if (strcmp(policies[i], "LRU") == 0) has_lru = true;
        if (strcmp(policies[i], "SIEVE") == 0) has_sieve = true;
    }

    TEST_ASSERT(has_fifo, "FIFO policy present");
    TEST_ASSERT(has_lru, "LRU policy present");
    TEST_ASSERT(has_sieve, "SIEVE policy present");

    for (int i = 1; i < 4; i++) {
        uint64_t accesses = parse_csv_uint64(lines[i], 3);
        TEST_ASSERT(accesses == 5, "All policies see same access count");
    }

    free_csv_lines(lines, line_count);
}

static void test_workload_and_seed_metadata(void) {
    write_trace_file("1\n2\n3\n");

    int result = run_benchmark_wrapper(TEST_TRACE_FILE, 10, TEST_OUTPUT_FILE, "uniform", "42");
    TEST_ASSERT(result == 0, "Benchmark succeeded with metadata");

    char **lines;
    int line_count;
    read_csv_lines(&lines, &line_count);

    for (int i = 1; i < 4; i++) {
        char workload[64];
        char seed[64];
        parse_csv_field(lines[i], 0, workload, sizeof(workload));
        parse_csv_field(lines[i], 2, seed, sizeof(seed));
        TEST_ASSERT(strcmp(workload, "uniform") == 0, "Workload metadata preserved");
        TEST_ASSERT(strcmp(seed, "42") == 0, "Seed metadata preserved");
    }

    free_csv_lines(lines, line_count);
}

int main(void) {
    printf("=========================================\n");
    printf(" Running Milestone 5 Benchmark Tests    \n");
    printf("=========================================\n\n");

    setup_test_dir();

    printf("[TEST] Tiny known trace with verifiable counts:\n");
    test_tiny_known_trace();
    printf("  -> Tiny trace test passed.\n\n");

    printf("[TEST] Repeated keys:\n");
    test_repeated_keys();
    printf("  -> Repeated keys test passed.\n\n");

    printf("[TEST] Capacity 1:\n");
    test_capacity_one();
    printf("  -> Capacity 1 test passed.\n\n");

    printf("[TEST] Key 0:\n");
    test_key_zero();
    printf("  -> Key 0 test passed.\n\n");

    printf("[TEST] Key UINT64_MAX:\n");
    test_key_uint64_max();
    printf("  -> UINT64_MAX test passed.\n\n");

    printf("[TEST] Empty trace:\n");
    test_empty_trace();
    printf("  -> Empty trace rejection passed.\n\n");

    printf("[TEST] Malformed input:\n");
    test_malformed_input();
    printf("  -> Malformed input rejection passed.\n\n");

    printf("[TEST] Missing trace file:\n");
    test_missing_trace_file();
    printf("  -> Missing file rejection passed.\n\n");

    printf("[TEST] Invalid capacity:\n");
    test_invalid_capacity();
    printf("  -> Invalid capacity rejection passed.\n\n");

    printf("[TEST] CSV header and row count:\n");
    test_csv_header_and_row_count();
    printf("  -> CSV format validation passed.\n\n");

    printf("[TEST] Metric invariants:\n");
    test_metric_invariants();
    printf("  -> Metric invariants validated.\n\n");

    printf("[TEST] Deterministic replay:\n");
    test_deterministic_replay();
    printf("  -> Deterministic replay verified.\n\n");

    printf("[TEST] All policies on same trace:\n");
    test_all_policies_on_same_trace();
    printf("  -> All policies executed correctly.\n\n");

    printf("[TEST] Workload and seed metadata:\n");
    test_workload_and_seed_metadata();
    printf("  -> Metadata preservation verified.\n\n");

    cleanup_test_dir();

    printf("=========================================\n");
    printf(" ALL BENCHMARK TESTS PASSED: %d / %d assertions OK\n", tests_passed, tests_run);
    printf("=========================================\n");

    return 0;
}
