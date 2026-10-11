#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tracegen.h"

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

/* =========================================================================
 * 1. Reproducibility & Seed Sensitivity Tests
 * ========================================================================= */

static void test_reproducibility_same_seed(void) {
    trace_config_t cfg;
    trace_config_init_defaults(&cfg, WORKLOAD_UNIFORM);
    cfg.num_accesses = 1000;
    cfg.key_range = 500;
    cfg.seed = 987654321ULL;

    uint64_t trace1[1000];
    uint64_t trace2[1000];

    TEST_ASSERT(trace_generate_array(&cfg, trace1, 1000) == 0, "Generated trace1");
    TEST_ASSERT(trace_generate_array(&cfg, trace2, 1000) == 0, "Generated trace2");

    int identical = 1;
    for (size_t i = 0; i < 1000; i++) {
        if (trace1[i] != trace2[i]) {
            identical = 0;
            break;
        }
    }
    TEST_ASSERT(identical == 1, "Same seed and parameters produced bit-identical trace");

    /* Also test Zipf reproducibility */
    cfg.workload = WORKLOAD_ZIPF;
    cfg.alpha = 1.2;
    TEST_ASSERT(trace_generate_array(&cfg, trace1, 1000) == 0, "Generated Zipf trace1");
    TEST_ASSERT(trace_generate_array(&cfg, trace2, 1000) == 0, "Generated Zipf trace2");

    identical = 1;
    for (size_t i = 0; i < 1000; i++) {
        if (trace1[i] != trace2[i]) {
            identical = 0;
            break;
        }
    }
    TEST_ASSERT(identical == 1, "Same seed produced bit-identical Zipf trace");
}

static void test_seed_sensitivity(void) {
    trace_config_t cfg1, cfg2;
    trace_config_init_defaults(&cfg1, WORKLOAD_UNIFORM);
    trace_config_init_defaults(&cfg2, WORKLOAD_UNIFORM);

    cfg1.num_accesses = 500;
    cfg1.key_range = 10000;
    cfg1.seed = 11111ULL;

    cfg2.num_accesses = 500;
    cfg2.key_range = 10000;
    cfg2.seed = 99999ULL;

    uint64_t trace1[500];
    uint64_t trace2[500];

    TEST_ASSERT(trace_generate_array(&cfg1, trace1, 500) == 0, "Generated trace1");
    TEST_ASSERT(trace_generate_array(&cfg2, trace2, 500) == 0, "Generated trace2");

    size_t matches = 0;
    for (size_t i = 0; i < 500; i++) {
        if (trace1[i] == trace2[i]) {
            matches++;
        }
    }
    /* Two random 500-element traces from range 10,000 should differ almost everywhere */
    TEST_ASSERT(matches < 10, "Different seeds produce distinctly different traces");
}

/* =========================================================================
 * 2. Access Count & Range Containment Tests
 * ========================================================================= */

static void test_access_count_and_range(void) {
    trace_config_t cfg;
    trace_config_init_defaults(&cfg, WORKLOAD_UNIFORM);
    cfg.num_accesses = 250;
    cfg.key_range = 50;
    cfg.seed = 42;

    uint64_t trace[250];
    TEST_ASSERT(trace_generate_array(&cfg, trace, 250) == 0, "Generated array");

    for (size_t i = 0; i < 250; i++) {
        TEST_ASSERT(trace[i] < 50, "Uniform key strictly within [0, key_range - 1]");
    }

    /* Single access edge case */
    cfg.num_accesses = 1;
    uint64_t single_trace[1];
    TEST_ASSERT(trace_generate_array(&cfg, single_trace, 1) == 0, "Generated single access");
    TEST_ASSERT(single_trace[0] < 50, "Single access within range");
}

/* =========================================================================
 * 3. Looping Workload Ordering
 * ========================================================================= */

static void test_looping_workload(void) {
    trace_config_t cfg;
    trace_config_init_defaults(&cfg, WORKLOAD_LOOP);
    cfg.num_accesses = 12;
    cfg.key_range = 4; /* Loop 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 */

    uint64_t trace[12];
    TEST_ASSERT(trace_generate_array(&cfg, trace, 12) == 0, "Generated loop trace");

    for (size_t i = 0; i < 12; i++) {
        uint64_t expected = i % 4;
        TEST_ASSERT(trace[i] == expected, "Loop key matches expected cyclic sequence");
    }
}

/* =========================================================================
 * 4. Sequential Scan Ordering
 * ========================================================================= */

static void test_scan_workload(void) {
    trace_config_t cfg;
    trace_config_init_defaults(&cfg, WORKLOAD_SCAN);
    cfg.num_accesses = 20;

    uint64_t trace[20];
    TEST_ASSERT(trace_generate_array(&cfg, trace, 20) == 0, "Generated scan trace");

    for (size_t i = 0; i < 20; i++) {
        TEST_ASSERT(trace[i] == (uint64_t)i, "Scan key matches consecutive sequence");
    }
}

/* =========================================================================
 * 5. Hot Set Followed by Scan Workload
 * ========================================================================= */

static void test_hot_scan_workload(void) {
    trace_config_t cfg;
    trace_config_init_defaults(&cfg, WORKLOAD_HOT_SCAN);
    cfg.num_accesses = 100;
    cfg.hot_size = 10;       /* Hot keys in [0, 9] */
    cfg.hot_fraction = 0.4;  /* 40 accesses in hot phase, 60 in scan phase */
    cfg.seed = 12345;

    uint64_t trace[100];
    TEST_ASSERT(trace_generate_array(&cfg, trace, 100) == 0, "Generated hot_scan trace");

    /* Phase 1: first 40 accesses must be within hot set [0, 9] */
    for (size_t i = 0; i < 40; i++) {
        TEST_ASSERT(trace[i] < 10, "Phase 1 key belongs to hot set [0, 9]");
    }

    /* Phase 2: next 60 accesses must be sequential scan starting at key 10 */
    for (size_t i = 40; i < 100; i++) {
        uint64_t expected = 10 + (i - 40);
        TEST_ASSERT(trace[i] == expected, "Phase 2 key matches sequential scan");
    }
}

/* =========================================================================
 * 6. Zipf Skewness Property
 * ========================================================================= */

static void test_zipf_skewness(void) {
    trace_config_t cfg;
    trace_config_init_defaults(&cfg, WORKLOAD_ZIPF);
    cfg.num_accesses = 50000;
    cfg.key_range = 100;
    cfg.alpha = 1.0;
    cfg.seed = 777;

    uint64_t *trace = malloc(cfg.num_accesses * sizeof(uint64_t));
    TEST_ASSERT(trace != NULL, "Allocated buffer for Zipf test");
    TEST_ASSERT(trace_generate_array(&cfg, trace, cfg.num_accesses) == 0, "Generated Zipf trace");

    uint64_t counts[100] = {0};
    for (size_t i = 0; i < cfg.num_accesses; i++) {
        TEST_ASSERT(trace[i] < 100, "Zipf key within range [0, 99]");
        counts[trace[i]]++;
    }

    /* In Zipf, lower ranks should have higher counts on average */
    TEST_ASSERT(counts[0] > counts[10], "Rank 0 frequency > Rank 10 frequency");
    TEST_ASSERT(counts[10] > counts[50], "Rank 10 frequency > Rank 50 frequency");
    TEST_ASSERT(counts[0] > counts[90], "Rank 0 frequency > Rank 90 frequency");

    /* Top 10% of keys (0-9) should capture a dominant fraction of requests (e.g. > 40%) */
    uint64_t top10_sum = 0;
    for (size_t i = 0; i < 10; i++) {
        top10_sum += counts[i];
    }
    double top10_fraction = (double)top10_sum / (double)cfg.num_accesses;
    TEST_ASSERT(top10_fraction > 0.40, "Top 10% keys account for > 40% of requests");

    free(trace);
}

/* =========================================================================
 * 7. Argument Validation & Error Handling
 * ========================================================================= */

static void test_argument_validation(void) {
    trace_config_t cfg;
    char err[256];

    /* Invalid: 0 accesses */
    trace_config_init_defaults(&cfg, WORKLOAD_UNIFORM);
    cfg.num_accesses = 0;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject 0 accesses");

    /* Invalid: 0 key range for uniform */
    trace_config_init_defaults(&cfg, WORKLOAD_UNIFORM);
    cfg.key_range = 0;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject 0 key range for uniform");

    /* Invalid: 0 key range for loop */
    trace_config_init_defaults(&cfg, WORKLOAD_LOOP);
    cfg.key_range = 0;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject 0 key range for loop");

    /* Invalid: negative or zero alpha for Zipf */
    trace_config_init_defaults(&cfg, WORKLOAD_ZIPF);
    cfg.alpha = 0.0;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject alpha 0.0 for Zipf");
    cfg.alpha = -1.5;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject negative alpha for Zipf");

    /* Invalid: hot fraction out of bounds */
    trace_config_init_defaults(&cfg, WORKLOAD_HOT_SCAN);
    cfg.hot_fraction = 0.0;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject hot fraction 0.0");
    cfg.hot_fraction = 1.0;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject hot fraction 1.0");

    /* Invalid: hot size 0 */
    cfg.hot_fraction = 0.5;
    cfg.hot_size = 0;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject hot size 0");

    /* Workload name parser tests */
    workload_type_t wt;
    TEST_ASSERT(trace_parse_workload_name("uniform", &wt) == 0 && wt == WORKLOAD_UNIFORM, "Parsed uniform");
    TEST_ASSERT(trace_parse_workload_name("zipf", &wt) == 0 && wt == WORKLOAD_ZIPF, "Parsed zipf");
    TEST_ASSERT(trace_parse_workload_name("loop", &wt) == 0 && wt == WORKLOAD_LOOP, "Parsed loop");
    TEST_ASSERT(trace_parse_workload_name("scan", &wt) == 0 && wt == WORKLOAD_SCAN, "Parsed scan");
    TEST_ASSERT(trace_parse_workload_name("hot_scan", &wt) == 0 && wt == WORKLOAD_HOT_SCAN, "Parsed hot_scan");
    TEST_ASSERT(trace_parse_workload_name("invalid_name", &wt) != 0, "Rejected invalid workload name");

    /* Invalid: non-finite Zipf alpha */
    trace_config_init_defaults(&cfg, WORKLOAD_ZIPF);
    cfg.alpha = NAN;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject NaN alpha for Zipf");
    cfg.alpha = INFINITY;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject +Inf alpha for Zipf");
    cfg.alpha = -INFINITY;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject -Inf alpha for Zipf");

    /* Invalid: non-finite hot fraction */
    trace_config_init_defaults(&cfg, WORKLOAD_HOT_SCAN);
    cfg.hot_fraction = NAN;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject NaN hot fraction");
    cfg.hot_fraction = INFINITY;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0, "Reject +Inf hot fraction");
}

/* =========================================================================
 * 8b. Hot-scan arithmetic boundaries
 * ========================================================================= */

static void test_hot_scan_arithmetic_boundaries(void) {
    trace_config_t cfg;
    char err[256];
    uint64_t trace[8];

    /* n=1, frac=0.5: floor product is 0, clamped to 1; no scan phase. */
    trace_config_init_defaults(&cfg, WORKLOAD_HOT_SCAN);
    cfg.num_accesses = 1;
    cfg.hot_size = 7;
    cfg.hot_fraction = 0.5;
    cfg.seed = 1;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) == 0,
                "Accept single-access hot_scan with frac 0.5");
    TEST_ASSERT(trace_generate_array(&cfg, trace, 1) == 0, "Generated n=1 hot_scan");
    TEST_ASSERT(trace[0] < 7, "Single hot_scan access stays in hot set");

    /* n=2, frac=0.5: one hot access, one scan key at hot_size. */
    cfg.num_accesses = 2;
    cfg.hot_size = 4;
    TEST_ASSERT(trace_generate_array(&cfg, trace, 2) == 0, "Generated n=2 hot_scan");
    TEST_ASSERT(trace[0] < 4, "First access in hot set");
    TEST_ASSERT(trace[1] == 4, "Scan phase starts at hot_size");

    /* Last scan key == UINT64_MAX is representable. */
    cfg.num_accesses = 2;
    cfg.hot_size = UINT64_MAX;
    cfg.hot_fraction = 0.5;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) == 0,
                "Accept hot_scan whose last key is UINT64_MAX");

    /* hot_size + scan offset would wrap. */
    cfg.num_accesses = 4;
    cfg.hot_size = UINT64_MAX;
    cfg.hot_fraction = 0.5; /* 2 hot, 2 scan => last key UINT64_MAX + 1 */
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0,
                "Reject hot_scan key overflow");
    TEST_ASSERT(trace_generate_array(&cfg, trace, 4) != 0,
                "Overflowing hot_scan does not write keys");

    /* Access count too large for an exact double product. */
    cfg.num_accesses = UINT64_C(9007199254740993);
    cfg.hot_size = 10;
    cfg.hot_fraction = 0.5;
    TEST_ASSERT(trace_config_validate(&cfg, err, sizeof(err)) != 0,
                "Reject hot_scan n beyond exact double integers");
}

/* =========================================================================
 * 8. File I/O and Plain-Text Format Verification
 * ========================================================================= */

static void test_file_output(void) {
    const char *test_path = "tests/test_trace_tmp.txt";

    trace_config_t cfg;
    trace_config_init_defaults(&cfg, WORKLOAD_LOOP);
    cfg.num_accesses = 5;
    cfg.key_range = 3;

    TEST_ASSERT(trace_generate_file(&cfg, test_path) == 0, "Wrote trace to file");

    FILE *fp = fopen(test_path, "r");
    TEST_ASSERT(fp != NULL, "Opened written trace file");

    uint64_t key;
    size_t count = 0;
    uint64_t expected[] = {0, 1, 2, 0, 1};

    while (fscanf(fp, "%" SCNu64, &key) == 1) {
        TEST_ASSERT(count < 5, "Not more than 5 keys");
        TEST_ASSERT(key == expected[count], "Key matches expected plain-text file content");
        count++;
    }
    fclose(fp);
    TEST_ASSERT(count == 5, "Exactly 5 keys read from file");

    /* Clean up temporary test file */
    unlink(test_path);
}

/* =========================================================================
 * 9. CLI parsing: exit status of the tracegen executable
 * ========================================================================= */

static int run_tracegen_cli(const char *args) {
    char cmd[2048];
    int n = snprintf(cmd, sizeof(cmd), "./bin/tracegen %s >/dev/null 2>&1", args);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        return -1;
    }
    int rc = system(cmd);
    if (rc == -1) {
        return -1;
    }
    if (!WIFEXITED(rc)) {
        return -1;
    }
    return WEXITSTATUS(rc);
}

static void test_cli_parsing_and_exit_status(void) {
    TEST_ASSERT(access("./bin/tracegen", X_OK) == 0, "tracegen CLI binary is present");

    /* Valid workloads, including seed 0. */
    TEST_ASSERT(run_tracegen_cli("-w uniform -n 20 -k 8 -s 0") == 0, "CLI uniform succeeded");
    TEST_ASSERT(run_tracegen_cli("-w zipf -n 20 -k 8 -a 1.0 -s 1") == 0, "CLI zipf succeeded");
    TEST_ASSERT(run_tracegen_cli("-w loop -n 20 -k 8") == 0, "CLI loop succeeded");
    TEST_ASSERT(run_tracegen_cli("-w scan -n 20") == 0, "CLI scan succeeded");
    TEST_ASSERT(run_tracegen_cli("-w hot_scan -n 20 --hot-size 4 --hot-frac 0.5 -s 0") == 0,
                "CLI hot_scan succeeded");

    /* Negative unsigned inputs (attached so getopt passes the string through). */
    TEST_ASSERT(run_tracegen_cli("--accesses=-1") != 0, "CLI rejects negative access count");
    TEST_ASSERT(run_tracegen_cli("--keys=-5") != 0, "CLI rejects negative key range");
    TEST_ASSERT(run_tracegen_cli("--seed=-1") != 0, "CLI rejects negative seed");
    TEST_ASSERT(run_tracegen_cli("--hot-size=-3") != 0, "CLI rejects negative hot-size");

    /* Zero where positive is required; seed 0 remains valid (tested above). */
    TEST_ASSERT(run_tracegen_cli("--accesses=0") != 0, "CLI rejects zero accesses");
    TEST_ASSERT(run_tracegen_cli("--keys=0") != 0, "CLI rejects zero key range");
    TEST_ASSERT(run_tracegen_cli("--hot-size=0 -w hot_scan") != 0, "CLI rejects zero hot-size");

    /* Empty, malformed, trailing junk, integer overflow. */
    TEST_ASSERT(run_tracegen_cli("--accesses=") != 0, "CLI rejects empty access count");
    TEST_ASSERT(run_tracegen_cli("--accesses=abc") != 0, "CLI rejects non-numeric accesses");
    TEST_ASSERT(run_tracegen_cli("--accesses=12xyz") != 0, "CLI rejects trailing junk on accesses");
    TEST_ASSERT(run_tracegen_cli("--keys=1.5") != 0, "CLI rejects non-integer key range");
    TEST_ASSERT(run_tracegen_cli("--seed=18446744073709551616") != 0, "CLI rejects seed overflow");
    TEST_ASSERT(run_tracegen_cli("--accesses=18446744073709551616") != 0,
                "CLI rejects access-count overflow");

    /* Floating-point: NaN, infinity, malformed, out of range. */
    TEST_ASSERT(run_tracegen_cli("-w zipf --alpha=nan") != 0, "CLI rejects NaN alpha");
    TEST_ASSERT(run_tracegen_cli("-w zipf --alpha=NaN") != 0, "CLI rejects NaN alpha (mixed case)");
    TEST_ASSERT(run_tracegen_cli("-w zipf --alpha=inf") != 0, "CLI rejects Inf alpha");
    TEST_ASSERT(run_tracegen_cli("-w zipf --alpha=infinity") != 0, "CLI rejects infinity alpha");
    TEST_ASSERT(run_tracegen_cli("-w zipf --alpha=-inf") != 0, "CLI rejects -Inf alpha");
    TEST_ASSERT(run_tracegen_cli("-w zipf --alpha=0") != 0, "CLI rejects zero alpha");
    TEST_ASSERT(run_tracegen_cli("-w zipf --alpha=-1.0") != 0, "CLI rejects negative alpha");
    TEST_ASSERT(run_tracegen_cli("-w zipf --alpha=1.0foo") != 0, "CLI rejects trailing junk on alpha");
    TEST_ASSERT(run_tracegen_cli("-w zipf --alpha=") != 0, "CLI rejects empty alpha");
    TEST_ASSERT(run_tracegen_cli("-w hot_scan --hot-frac=nan") != 0, "CLI rejects NaN hot-frac");
    TEST_ASSERT(run_tracegen_cli("-w hot_scan --hot-frac=inf") != 0, "CLI rejects Inf hot-frac");
    TEST_ASSERT(run_tracegen_cli("-w hot_scan --hot-frac=0") != 0, "CLI rejects hot-frac 0");
    TEST_ASSERT(run_tracegen_cli("-w hot_scan --hot-frac=1") != 0, "CLI rejects hot-frac 1");
    TEST_ASSERT(run_tracegen_cli("-w hot_scan --hot-frac=1.5") != 0, "CLI rejects hot-frac > 1");
    TEST_ASSERT(run_tracegen_cli("-w hot_scan --hot-frac=-0.2") != 0, "CLI rejects negative hot-frac");
    TEST_ASSERT(run_tracegen_cli("-w hot_scan --hot-frac=0.5junk") != 0,
                "CLI rejects trailing junk on hot-frac");

    /* Hot-scan arithmetic overflow at the CLI (must fail before producing a trace). */
    TEST_ASSERT(run_tracegen_cli(
                    "-w hot_scan -n 4 --hot-size=18446744073709551615 --hot-frac=0.5") != 0,
                "CLI rejects overflowing hot_scan configuration");

    /* Invalid configs must not create or truncate an output file. */
    const char *overflow_path = "tests/test_hot_scan_overflow.txt";
    unlink(overflow_path);
    char overflow_args[512];
    snprintf(overflow_args, sizeof(overflow_args),
             "-w hot_scan -n 4 --hot-size=18446744073709551615 --hot-frac=0.5 -o %s",
             overflow_path);
    TEST_ASSERT(run_tracegen_cli(overflow_args) != 0,
                "CLI overflow with -o still fails");
    TEST_ASSERT(access(overflow_path, F_OK) != 0,
                "Invalid hot_scan did not create an output file");
}

int main(void) {
    printf("=========================================\n");
    printf(" Running Milestone 4 Trace Generator Tests\n");
    printf("=========================================\n\n");

    printf("[TEST] Reproducibility with Same Seed:\n");
    test_reproducibility_same_seed();
    printf("  -> Same seed and parameters produced bit-identical traces.\n\n");

    printf("[TEST] Seed Sensitivity:\n");
    test_seed_sensitivity();
    printf("  -> Different seeds produced distinct random sequences.\n\n");

    printf("[TEST] Access Count and Range Containment:\n");
    test_access_count_and_range();
    printf("  -> Access count and key range bounds verified.\n\n");

    printf("[TEST] Looping Workload Ordering:\n");
    test_looping_workload();
    printf("  -> Looping order verified.\n\n");

    printf("[TEST] Sequential Scan Ordering:\n");
    test_scan_workload();
    printf("  -> Sequential scan verified.\n\n");

    printf("[TEST] Hot Set Followed by Scan:\n");
    test_hot_scan_workload();
    printf("  -> Hot phase and sequential scan phase verified.\n\n");

    printf("[TEST] Zipf Skew Distribution:\n");
    test_zipf_skewness();
    printf("  -> Exact discrete Zipf frequency skew verified.\n\n");

    printf("[TEST] Argument Validation & Error Handling:\n");
    test_argument_validation();
    printf("  -> Invalid arguments and edge cases rejected cleanly.\n\n");

    printf("[TEST] Hot-Scan Arithmetic Boundaries:\n");
    test_hot_scan_arithmetic_boundaries();
    printf("  -> Hot-scan overflow and boundary cases verified.\n\n");

    printf("[TEST] File Output and Plain-Text Format:\n");
    test_file_output();
    printf("  -> Plain-text file output verified and cleaned up.\n\n");

    printf("[TEST] CLI Parsing and Exit Status:\n");
    test_cli_parsing_and_exit_status();
    printf("  -> tracegen CLI accepted valid workloads and rejected invalid inputs.\n\n");

    printf("=========================================\n");
    printf(" ALL TRACEGEN TESTS PASSED: %d / %d assertions OK\n", tests_passed, tests_run);
    printf("=========================================\n");

    return 0;
}
