#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"

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
 * 1. Capacity and Validation Tests
 * ========================================================================= */

static void test_invalid_capacity_and_policies(void) {
    /* Capacity 0 must be rejected */
    TEST_ASSERT(cache_create(0, CACHE_POLICY_FIFO) == NULL, "Capacity 0 FIFO rejected");
    TEST_ASSERT(cache_create(0, CACHE_POLICY_LRU) == NULL, "Capacity 0 LRU rejected");
    TEST_ASSERT(cache_create(0, CACHE_POLICY_SIEVE) == NULL, "Capacity 0 SIEVE rejected");

    /* Invalid policy enum must be rejected */
    TEST_ASSERT(cache_create(10, (cache_policy_type_t)99) == NULL, "Invalid policy enum rejected");

    /* Valid creation */
    cache_t *c = cache_create(5, CACHE_POLICY_FIFO);
    TEST_ASSERT(c != NULL, "Valid cache created");
    TEST_ASSERT(cache_capacity(c) == 5, "Capacity is 5");
    TEST_ASSERT(cache_size(c) == 0, "Initial size is 0");

    cache_destroy(c);
}

/* =========================================================================
 * 2. FIFO Deterministic Tests
 * ========================================================================= */

static void test_fifo_policy(void) {
    cache_t *c = cache_create(3, CACHE_POLICY_FIFO);
    TEST_ASSERT(c != NULL, "Created FIFO cache with capacity 3");

    /* Misses: 1, 2, 3 -> Queue: 3 (head) -> 2 -> 1 (tail) */
    TEST_ASSERT(!cache_access(c, 1), "Access 1: miss");
    TEST_ASSERT(!cache_access(c, 2), "Access 2: miss");
    TEST_ASSERT(!cache_access(c, 3), "Access 3: miss");
    TEST_ASSERT(cache_size(c) == 3, "Cache is full (size 3)");

    /* Hit: 2 -> FIFO leaves insertion order completely unchanged */
    TEST_ASSERT(cache_access(c, 2), "Access 2: hit");
    TEST_ASSERT(c->list.tail->key == 1, "Tail is still 1 (oldest inserted)");
    TEST_ASSERT(c->list.head->key == 3, "Head is still 3");

    /* Miss: 4 -> Cache full, FIFO evicts oldest entry (1) */
    TEST_ASSERT(!cache_access(c, 4), "Access 4: miss");
    TEST_ASSERT(!cache_contains(c, 1), "Key 1 was evicted");
    TEST_ASSERT(cache_contains(c, 2), "Key 2 retained");
    TEST_ASSERT(cache_contains(c, 3), "Key 3 retained");
    TEST_ASSERT(cache_contains(c, 4), "Key 4 present");
    TEST_ASSERT(c->list.tail->key == 2, "Tail is now 2 (next oldest)");
    TEST_ASSERT(c->list.head->key == 4, "Head is now 4");

    /* Miss: 5 -> Cache full, FIFO evicts next oldest entry (2) */
    TEST_ASSERT(!cache_access(c, 5), "Access 5: miss");
    TEST_ASSERT(!cache_contains(c, 2), "Key 2 was evicted");
    TEST_ASSERT(cache_contains(c, 3), "Key 3 retained");
    TEST_ASSERT(cache_contains(c, 4), "Key 4 retained");
    TEST_ASSERT(cache_contains(c, 5), "Key 5 present");
    TEST_ASSERT(c->list.tail->key == 3, "Tail is now 3");

    /* Verify statistics */
    const cache_stats_t *s = cache_get_stats(c);
    TEST_ASSERT(s->accesses == 6, "Total accesses is 6");
    TEST_ASSERT(s->hits == 1, "Total hits is 1");
    TEST_ASSERT(s->misses == 5, "Total misses is 5");
    TEST_ASSERT(s->evictions == 2, "Total evictions is 2");

    cache_destroy(c);
}

/* =========================================================================
 * 3. LRU Deterministic Tests
 * ========================================================================= */

static void test_lru_policy(void) {
    cache_t *c = cache_create(3, CACHE_POLICY_LRU);
    TEST_ASSERT(c != NULL, "Created LRU cache with capacity 3");

    /* Misses: 1, 2, 3 -> Queue: 3 (head) -> 2 -> 1 (tail) */
    TEST_ASSERT(!cache_access(c, 1), "Access 1: miss");
    TEST_ASSERT(!cache_access(c, 2), "Access 2: miss");
    TEST_ASSERT(!cache_access(c, 3), "Access 3: miss");

    /* Hit: 1 -> LRU promotes 1 to head! Queue: 1 (head) -> 3 -> 2 (tail) */
    TEST_ASSERT(cache_access(c, 1), "Access 1: hit");
    TEST_ASSERT(c->list.head->key == 1, "Key 1 promoted to head");
    TEST_ASSERT(c->list.tail->key == 2, "Tail is now 2 (LRU node)");

    /* Miss: 4 -> Cache full, LRU evicts tail (2) (contrast with FIFO which evicted 1) */
    TEST_ASSERT(!cache_access(c, 4), "Access 4: miss");
    TEST_ASSERT(!cache_contains(c, 2), "Key 2 was evicted by LRU");
    TEST_ASSERT(cache_contains(c, 1), "Key 1 retained (promoted by prior hit)");
    TEST_ASSERT(cache_contains(c, 3), "Key 3 retained");
    TEST_ASSERT(cache_contains(c, 4), "Key 4 admitted");
    /* Queue: 4 (head) -> 1 -> 3 (tail) */
    TEST_ASSERT(c->list.head->key == 4, "Head is 4");
    TEST_ASSERT(c->list.tail->key == 3, "Tail is 3");

    /* Hit: 3 -> promotes 3 to head! Queue: 3 (head) -> 4 -> 1 (tail) */
    TEST_ASSERT(cache_access(c, 3), "Access 3: hit");
    TEST_ASSERT(c->list.head->key == 3, "Key 3 promoted to head");
    TEST_ASSERT(c->list.tail->key == 1, "Tail is now 1 (LRU)");

    /* Miss: 5 -> evicts tail (1) */
    TEST_ASSERT(!cache_access(c, 5), "Access 5: miss");
    TEST_ASSERT(!cache_contains(c, 1), "Key 1 was evicted");
    TEST_ASSERT(cache_contains(c, 3), "Key 3 retained");
    TEST_ASSERT(cache_contains(c, 4), "Key 4 retained");
    TEST_ASSERT(cache_contains(c, 5), "Key 5 admitted");

    const cache_stats_t *s = cache_get_stats(c);
    TEST_ASSERT(s->accesses == 7, "Total accesses is 7");
    TEST_ASSERT(s->hits == 2, "Total hits is 2");
    TEST_ASSERT(s->misses == 5, "Total misses is 5");
    TEST_ASSERT(s->evictions == 2, "Total evictions is 2");

    cache_destroy(c);
}

/* =========================================================================
 * 4. SIEVE Deterministic Tests (Algorithm 1 Verification)
 * ========================================================================= */

static void test_sieve_policy(void) {
    cache_t *c = cache_create(3, CACHE_POLICY_SIEVE);
    TEST_ASSERT(c != NULL, "Created SIEVE cache with capacity 3");

    /*
     * Step 1: Misses: 1, 2, 3
     * All newly admitted entries have visited = false.
     * Queue: 3(v=0) -> 2(v=0) -> 1(v=0)
     * Head = 3, Tail = 1. Hand = NULL.
     */
    TEST_ASSERT(!cache_access(c, 1), "Access 1: miss");
    TEST_ASSERT(!cache_access(c, 2), "Access 2: miss");
    TEST_ASSERT(!cache_access(c, 3), "Access 3: miss");
    TEST_ASSERT(cache_size(c) == 3, "Cache full");
    TEST_ASSERT(c->hand == NULL, "Initial hand is NULL");

    /*
     * Step 2: Hit: 2
     * In SIEVE, a hit sets visited = true and leaves queue order untouched.
     * Queue: 3(v=0) -> 2(v=1) -> 1(v=0)
     */
    TEST_ASSERT(cache_access(c, 2), "Access 2: hit");
    dlist_node_t *node2 = ht_lookup(c->ht, 2);
    TEST_ASSERT(node2 != NULL && node2->visited == true, "Node 2 visited bit is 1");
    TEST_ASSERT(c->list.head->key == 3, "Head remains 3 (no eager promotion)");
    TEST_ASSERT(c->list.tail->key == 1, "Tail remains 1");

    /*
     * Step 3: Miss: 4 (Eviction #1)
     * Hand is NULL, so scan starts at tail (node 1).
     * Node 1 has visited == 0 -> victim is node 1!
     * Before unlinking 1, hand is saved as 1->prev (node 2).
     * Victim 1 is detached and freed.
     * New node 4 admitted at head with visited = 0.
     * Queue: 4(v=0) -> 3(v=0) -> 2(v=1)
     * Hand points to node 2.
     */
    TEST_ASSERT(!cache_access(c, 4), "Access 4: miss");
    TEST_ASSERT(!cache_contains(c, 1), "Key 1 was evicted");
    TEST_ASSERT(cache_contains(c, 2), "Key 2 retained");
    TEST_ASSERT(cache_contains(c, 3), "Key 3 retained");
    TEST_ASSERT(cache_contains(c, 4), "Key 4 present");
    TEST_ASSERT(c->hand != NULL && c->hand->key == 2, "Hand points to node 2");

    /*
     * Step 4: Miss: 5 (Eviction #2 - Lazy promotion & second chance)
     * Hand is at node 2.
     * Node 2 has visited == 1 -> cleared to 0, advance hand to 2->prev (node 3).
     * Node 3 has visited == 0 -> victim is node 3!
     * Before unlinking 3, hand is saved as 3->prev (node 4).
     * Victim 3 is detached and freed.
     * New node 5 admitted at head with visited = 0.
     * Queue: 5(v=0) -> 4(v=0) -> 2(v=0)
     * Hand points to node 4.
     *
     * Note: Node 2 survived because of lazy promotion!
     * (Under LRU, node 3 was accessed more recently than 2, but under SIEVE,
     * node 2 survived because its visited bit was set).
     */
    TEST_ASSERT(!cache_access(c, 5), "Access 5: miss");
    TEST_ASSERT(!cache_contains(c, 3), "Key 3 was evicted");
    TEST_ASSERT(cache_contains(c, 2), "Key 2 survived eviction via visited bit");
    TEST_ASSERT(cache_contains(c, 4), "Key 4 retained");
    TEST_ASSERT(cache_contains(c, 5), "Key 5 present");
    TEST_ASSERT(c->hand != NULL && c->hand->key == 4, "Hand points to node 4");

    /*
     * Step 5: Miss: 6 (Eviction #3)
     * Hand is at node 4.
     * Node 4 has visited == 0 -> victim is node 4!
     * Before unlinking 4, hand is saved as 4->prev (node 5).
     * Victim 4 is detached and freed.
     * New node 6 admitted at head with visited = 0.
     * Queue: 6(v=0) -> 5(v=0) -> 2(v=0)
     * Hand points to node 5.
     */
    TEST_ASSERT(!cache_access(c, 6), "Access 6: miss");
    TEST_ASSERT(!cache_contains(c, 4), "Key 4 was evicted");
    TEST_ASSERT(cache_contains(c, 2), "Key 2 still in cache");
    TEST_ASSERT(cache_contains(c, 5), "Key 5 retained");
    TEST_ASSERT(cache_contains(c, 6), "Key 6 present");
    TEST_ASSERT(c->hand != NULL && c->hand->key == 5, "Hand points to node 5");

    const cache_stats_t *s = cache_get_stats(c);
    TEST_ASSERT(s->accesses == 7, "Total accesses is 7");
    TEST_ASSERT(s->hits == 1, "Total hits is 1");
    TEST_ASSERT(s->misses == 6, "Total misses is 6");
    TEST_ASSERT(s->evictions == 3, "Total evictions is 3");

    cache_destroy(c);
}

/* =========================================================================
 * 5. SIEVE Wraparound and All-Visited Tests
 * ========================================================================= */

static void test_sieve_wraparound(void) {
    cache_t *c = cache_create(2, CACHE_POLICY_SIEVE);
    TEST_ASSERT(c != NULL, "Created SIEVE cache with capacity 2");

    /* Fill cache: 10, 20 -> Queue: 20(v=0) -> 10(v=0) */
    TEST_ASSERT(!cache_access(c, 10), "Access 10: miss");
    TEST_ASSERT(!cache_access(c, 20), "Access 20: miss");

    /* Hit both items so visited bits are all 1 */
    TEST_ASSERT(cache_access(c, 10), "Access 10: hit -> visited = 1");
    TEST_ASSERT(cache_access(c, 20), "Access 20: hit -> visited = 1");

    /*
     * Miss: 30
     * Eviction scan:
     * - Hand starts at tail (10). visited == 1 -> set to 0, advance to 20.
     * - Node 20 has visited == 1 -> set to 0, advance to NULL.
     * - Wraparound: obj wraps back to tail (10)!
     * - Node 10 now has visited == 0 -> victim is 10!
     * - Hand saved as 10->prev (node 20) before unlinking 10.
     * - 10 evicted. 30 admitted at head with v=0.
     * Queue: 30(v=0) -> 20(v=0)
     * Hand points to node 20.
     */
    TEST_ASSERT(!cache_access(c, 30), "Access 30: miss with wraparound");
    TEST_ASSERT(!cache_contains(c, 10), "Key 10 was evicted after wraparound");
    TEST_ASSERT(cache_contains(c, 20), "Key 20 retained with visited cleared");
    TEST_ASSERT(cache_contains(c, 30), "Key 30 present");
    TEST_ASSERT(c->hand != NULL && c->hand->key == 20, "Hand points to node 20");

    /*
     * Eviction when hand is at head:
     * Current queue: 30(head, v=0) -> 20(tail, v=0). Hand is at 20.
     * Next access: 40 (miss).
     * Hand is at 20 (v=0) -> victim is 20!
     * Next hand = 20->prev (node 30).
     * 20 evicted. 40 admitted at head with v=0.
     * Queue: 40(head, v=0) -> 30(tail, v=0). Hand is at 30 (tail).
     */
    TEST_ASSERT(!cache_access(c, 40), "Access 40: miss");
    TEST_ASSERT(!cache_contains(c, 20), "Key 20 evicted");
    TEST_ASSERT(cache_contains(c, 30), "Key 30 retained");
    TEST_ASSERT(cache_contains(c, 40), "Key 40 present");

    cache_destroy(c);
}

/* =========================================================================
 * 6. Capacity 1 Edge Case (FIFO, LRU, SIEVE)
 * ========================================================================= */

static void test_capacity_one_all_policies(cache_policy_type_t policy) {
    cache_t *c = cache_create(1, policy);
    TEST_ASSERT(c != NULL, "Created capacity 1 cache");

    /* Access 100: miss */
    TEST_ASSERT(!cache_access(c, 100), "C=1 Access 100: miss");
    TEST_ASSERT(cache_contains(c, 100), "C=1 contains 100");
    TEST_ASSERT(cache_size(c) == 1, "C=1 size is 1");

    /* Access 100: hit */
    TEST_ASSERT(cache_access(c, 100), "C=1 Access 100: hit");

    /* Access 200: miss -> evicts 100 */
    TEST_ASSERT(!cache_access(c, 200), "C=1 Access 200: miss & evicts 100");
    TEST_ASSERT(!cache_contains(c, 100), "C=1 100 is evicted");
    TEST_ASSERT(cache_contains(c, 200), "C=1 contains 200");

    /* Access 200: hit */
    TEST_ASSERT(cache_access(c, 200), "C=1 Access 200: hit");

    /* Access 300: miss -> evicts 200 */
    TEST_ASSERT(!cache_access(c, 300), "C=1 Access 300: miss & evicts 200");
    TEST_ASSERT(!cache_contains(c, 200), "C=1 200 is evicted");
    TEST_ASSERT(cache_contains(c, 300), "C=1 contains 300");

    const cache_stats_t *s = cache_get_stats(c);
    TEST_ASSERT(s->accesses == 5, "C=1 accesses is 5");
    TEST_ASSERT(s->hits == 2, "C=1 hits is 2");
    TEST_ASSERT(s->misses == 3, "C=1 misses is 3");
    TEST_ASSERT(s->evictions == 2, "C=1 evictions is 2");

    cache_destroy(c);
}

/* =========================================================================
 * 7. Repeated Access to Single Key
 * ========================================================================= */

static void test_repeated_access_single_key(cache_policy_type_t policy) {
    cache_t *c = cache_create(5, policy);
    TEST_ASSERT(c != NULL, "Created cache");

    /* 50 accesses to key 999: 1 miss followed by 49 hits */
    for (int i = 0; i < 50; i++) {
        bool hit = cache_access(c, 999);
        if (i == 0) {
            TEST_ASSERT(!hit, "First access is miss");
        } else {
            TEST_ASSERT(hit, "Subsequent access is hit");
        }
    }

    TEST_ASSERT(cache_size(c) == 1, "Cache size remains 1");
    const cache_stats_t *s = cache_get_stats(c);
    TEST_ASSERT(s->accesses == 50, "Accesses == 50");
    TEST_ASSERT(s->hits == 49, "Hits == 49");
    TEST_ASSERT(s->misses == 1, "Misses == 1");
    TEST_ASSERT(s->evictions == 0, "Evictions == 0");

    cache_destroy(c);
}

/* =========================================================================
 * 8. Clean Destruction with Various Fill States
 * ========================================================================= */

static void test_clean_destruction_various_states(void) {
    cache_policy_type_t policies[] = {CACHE_POLICY_FIFO, CACHE_POLICY_LRU, CACHE_POLICY_SIEVE};

    for (int p = 0; p < 3; p++) {
        /* Empty cache destruction */
        cache_t *c_empty = cache_create(10, policies[p]);
        cache_destroy(c_empty);

        /* Partially filled cache destruction */
        cache_t *c_partial = cache_create(10, policies[p]);
        cache_access(c_partial, 1);
        cache_access(c_partial, 2);
        cache_destroy(c_partial);

        /* Full cache destruction after evictions */
        cache_t *c_full = cache_create(3, policies[p]);
        for (uint64_t k = 1; k <= 10; k++) {
            cache_access(c_full, k);
        }
        cache_destroy(c_full);
    }
}

int main(void) {
    printf("=========================================\n");
    printf(" Running Milestone 3 Cache Policy Tests  \n");
    printf("=========================================\n\n");

    printf("[TEST] Capacity and Parameter Validation:\n");
    test_invalid_capacity_and_policies();
    printf("  -> Invalid capacity and parameter validation passed.\n\n");

    printf("[TEST] FIFO Eviction Policy:\n");
    test_fifo_policy();
    printf("  -> FIFO deterministic insertion-order eviction passed.\n\n");

    printf("[TEST] LRU Eviction Policy:\n");
    test_lru_policy();
    printf("  -> LRU deterministic hit-promotion and tail-eviction passed.\n\n");

    printf("[TEST] SIEVE Eviction Policy (NSDI '24 Algorithm 1):\n");
    test_sieve_policy();
    printf("  -> SIEVE lazy promotion and hand-sifting passed.\n");
    test_sieve_wraparound();
    printf("  -> SIEVE hand wraparound and all-visited cases passed.\n\n");

    printf("[TEST] Capacity 1 Edge Case:\n");
    test_capacity_one_all_policies(CACHE_POLICY_FIFO);
    test_capacity_one_all_policies(CACHE_POLICY_LRU);
    test_capacity_one_all_policies(CACHE_POLICY_SIEVE);
    printf("  -> Capacity 1 tests passed for FIFO, LRU, and SIEVE.\n\n");

    printf("[TEST] Repeated Single-Key Access:\n");
    test_repeated_access_single_key(CACHE_POLICY_FIFO);
    test_repeated_access_single_key(CACHE_POLICY_LRU);
    test_repeated_access_single_key(CACHE_POLICY_SIEVE);
    printf("  -> Repeated access tests passed for all policies.\n\n");

    printf("[TEST] Clean Destruction Across Cache States:\n");
    test_clean_destruction_various_states();
    printf("  -> Clean destruction passed for empty, partial, and full states.\n\n");

    printf("=========================================\n");
    printf(" ALL POLICY TESTS PASSED: %d / %d assertions OK\n", tests_passed, tests_run);
    printf("=========================================\n");

    return 0;
}
