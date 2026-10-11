#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dlist.h"
#include "hashtable.h"

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

static void test_structure_sizes(void) {
    printf("[INFO] Structure Sizing & Alignment (System-specific measurements):\n");
    printf("  sizeof(dlist_node_t) = %zu bytes\n", sizeof(dlist_node_t));
    printf("  sizeof(dlist_t)      = %zu bytes\n", sizeof(dlist_t));
    printf("  sizeof(hashtable_t)  = %zu bytes\n", sizeof(hashtable_t));
    printf("  -> Sizes reported without hard-coded layout assertions.\n\n");
}

/* =========================================================================
 * Doubly Linked List Tests
 * ========================================================================= */

static void test_dlist_empty(void) {
    dlist_t list;
    dlist_init(&list);

    TEST_ASSERT(list.head == NULL, "Empty list head is NULL");
    TEST_ASSERT(list.tail == NULL, "Empty list tail is NULL");
    TEST_ASSERT(list.size == 0, "Empty list size is 0");

    dlist_remove(&list, NULL);
    dlist_move_to_head(&list, NULL);
    dlist_free_all(&list);

    TEST_ASSERT(list.size == 0, "List remains empty after no-ops");
}

static void test_dlist_single_element(void) {
    dlist_t list;
    dlist_init(&list);

    dlist_node_t *n1 = dlist_node_create(100);
    TEST_ASSERT(n1 != NULL, "Node allocation succeeded");
    TEST_ASSERT(n1->key == 100, "Node key matches");
    TEST_ASSERT(!n1->visited, "Node initially unvisited");
    TEST_ASSERT(!n1->in_list, "Node initially not in list");
    TEST_ASSERT(!n1->in_ht, "Node initially not in hash table");

    TEST_ASSERT(dlist_insert_head(&list, n1), "Inserted n1 at head");
    TEST_ASSERT(n1->in_list, "n1 marked in_list");
    TEST_ASSERT(list.head == n1, "Single element is head");
    TEST_ASSERT(list.tail == n1, "Single element is tail");
    TEST_ASSERT(n1->prev == NULL, "Single element prev is NULL");
    TEST_ASSERT(n1->next == NULL, "Single element next is NULL");
    TEST_ASSERT(list.size == 1, "List size is 1");

    /* Reject inserting same node again into the list */
    TEST_ASSERT(!dlist_insert_head(&list, n1), "Reject duplicate insertion of same node into list");
    TEST_ASSERT(list.size == 1, "List size remains 1");

    /* Attempting to free a node while linked in list must be rejected */
    TEST_ASSERT(!dlist_node_free(n1), "dlist_node_free rejected while node->in_list is true");

    /* Move to head when already head is a no-op */
    dlist_move_to_head(&list, n1);
    TEST_ASSERT(list.head == n1 && list.tail == n1, "Move head when alone is no-op");

    dlist_remove(&list, n1);
    TEST_ASSERT(!n1->in_list, "n1->in_list cleared after removal");
    TEST_ASSERT(list.head == NULL, "List empty after removing sole element");
    TEST_ASSERT(list.tail == NULL, "Tail NULL after removing sole element");
    TEST_ASSERT(list.size == 0, "Size 0 after removing sole element");

    /* Once unlinked, freeing must succeed */
    TEST_ASSERT(dlist_node_free(n1), "dlist_node_free succeeds when unlinked");
}

static void test_dlist_multi_element_and_removals(void) {
    dlist_t list;
    dlist_init(&list);

    dlist_node_t *n1 = dlist_node_create(1);
    dlist_node_t *n2 = dlist_node_create(2);
    dlist_node_t *n3 = dlist_node_create(3);

    /* Insert order: 1, then 2, then 3 -> queue: 3 (head) -> 2 -> 1 (tail) */
    TEST_ASSERT(dlist_insert_head(&list, n1), "Inserted 1");
    TEST_ASSERT(dlist_insert_head(&list, n2), "Inserted 2");
    TEST_ASSERT(dlist_insert_head(&list, n3), "Inserted 3");

    TEST_ASSERT(list.size == 3, "List size is 3");
    TEST_ASSERT(list.head == n3, "Head is 3");
    TEST_ASSERT(list.tail == n1, "Tail is 1");

    /* Verify next traversal (head to tail): 3 -> 2 -> 1 */
    TEST_ASSERT(n3->next == n2, "3->next is 2");
    TEST_ASSERT(n2->next == n1, "2->next is 1");
    TEST_ASSERT(n1->next == NULL, "1->next is NULL (tail)");

    /* Verify prev traversal (tail to head): 1 -> 2 -> 3 */
    TEST_ASSERT(n1->prev == n2, "1->prev is 2");
    TEST_ASSERT(n2->prev == n3, "2->prev is 3");
    TEST_ASSERT(n3->prev == NULL, "3->prev is NULL (head)");

    /* Case A: Remove middle element (2) */
    dlist_remove(&list, n2);
    TEST_ASSERT(!n2->in_list, "n2 no longer in list");
    TEST_ASSERT(list.size == 2, "Size is 2 after middle remove");
    TEST_ASSERT(list.head == n3, "Head is still 3");
    TEST_ASSERT(list.tail == n1, "Tail is still 1");
    TEST_ASSERT(n3->next == n1, "3->next updated to 1");
    TEST_ASSERT(n1->prev == n3, "1->prev updated to 3");
    TEST_ASSERT(dlist_node_free(n2), "Freed unlinked middle node");

    /* Case B: Remove head element (3) */
    dlist_remove(&list, n3);
    TEST_ASSERT(!n3->in_list, "n3 no longer in list");
    TEST_ASSERT(list.size == 1, "Size is 1 after head remove");
    TEST_ASSERT(list.head == n1, "Head is now 1");
    TEST_ASSERT(list.tail == n1, "Tail is now 1");
    TEST_ASSERT(n1->prev == NULL, "1->prev is NULL");
    TEST_ASSERT(n1->next == NULL, "1->next is NULL");
    TEST_ASSERT(dlist_node_free(n3), "Freed unlinked head node");

    /* Case C: Remove remaining tail element (1) */
    dlist_remove(&list, n1);
    TEST_ASSERT(!n1->in_list, "n1 no longer in list");
    TEST_ASSERT(list.size == 0, "List is empty");
    TEST_ASSERT(list.head == NULL, "Head is NULL");
    TEST_ASSERT(list.tail == NULL, "Tail is NULL");
    TEST_ASSERT(dlist_node_free(n1), "Freed unlinked tail node");
}

static void test_dlist_move_to_head(void) {
    dlist_t list;
    dlist_init(&list);

    dlist_node_t *n1 = dlist_node_create(1);
    dlist_node_t *n2 = dlist_node_create(2);
    dlist_node_t *n3 = dlist_node_create(3);

    dlist_insert_head(&list, n1);
    dlist_insert_head(&list, n2);
    dlist_insert_head(&list, n3);
    /* Current order: 3 (head) -> 2 -> 1 (tail) */

    /* Move tail (1) to head */
    dlist_move_to_head(&list, n1);
    /* Expected order: 1 (head) -> 3 -> 2 (tail) */
    TEST_ASSERT(list.head == n1, "1 is now head");
    TEST_ASSERT(list.tail == n2, "2 is now tail");
    TEST_ASSERT(n1->next == n3, "1->next is 3");
    TEST_ASSERT(n3->prev == n1, "3->prev is 1");
    TEST_ASSERT(n3->next == n2, "3->next is 2");
    TEST_ASSERT(n2->prev == n3, "2->prev is 3");
    TEST_ASSERT(n2->next == NULL, "2->next is NULL");

    /* Move middle (3) to head */
    dlist_move_to_head(&list, n3);
    /* Expected order: 3 (head) -> 1 -> 2 (tail) */
    TEST_ASSERT(list.head == n3, "3 is now head");
    TEST_ASSERT(list.tail == n2, "2 remains tail");
    TEST_ASSERT(n3->next == n1, "3->next is 1");
    TEST_ASSERT(n1->prev == n3, "1->prev is 3");
    TEST_ASSERT(n1->next == n2, "1->next is 2");
    TEST_ASSERT(n2->prev == n1, "2->prev is 1");

    dlist_free_all(&list);
    TEST_ASSERT(list.size == 0, "List size 0 after dlist_free_all");
    TEST_ASSERT(list.head == NULL, "Head NULL after free_all");
    TEST_ASSERT(list.tail == NULL, "Tail NULL after free_all");

    /* Test owner explicitly frees the detached nodes */
    TEST_ASSERT(dlist_node_free(n1), "Freed detached node n1");
    TEST_ASSERT(dlist_node_free(n2), "Freed detached node n2");
    TEST_ASSERT(dlist_node_free(n3), "Freed detached node n3");
}

/* =========================================================================
 * Hash Table Tests
 * ========================================================================= */

static void test_ht_capacity_and_bucket_sizing(void) {
    /* Capacity 0 must be rejected */
    TEST_ASSERT(ht_calc_bucket_count(0) == 0, "Capacity 0 gives 0 buckets");
    TEST_ASSERT(ht_create(0) == NULL, "ht_create(0) returns NULL");

    /* Minimum bucket count is 16 */
    TEST_ASSERT(ht_calc_bucket_count(1) == 16, "C=1 gives 16 buckets");
    TEST_ASSERT(ht_calc_bucket_count(7) == 16, "C=7 gives 16 buckets");
    TEST_ASSERT(ht_calc_bucket_count(8) == 16, "C=8 gives 16 buckets");

    /* Powers of 2 derived from >= 2 * C */
    TEST_ASSERT(ht_calc_bucket_count(9) == 32, "C=9 gives 32 buckets");
    TEST_ASSERT(ht_calc_bucket_count(16) == 32, "C=16 gives 32 buckets");
    TEST_ASSERT(ht_calc_bucket_count(17) == 64, "C=17 gives 64 buckets");
    TEST_ASSERT(ht_calc_bucket_count(100) == 256, "C=100 gives 256 buckets");

    /* Overflow checks */
    TEST_ASSERT(ht_calc_bucket_count(SIZE_MAX) == 0, "SIZE_MAX gives 0 buckets (overflow safe)");
    TEST_ASSERT(ht_calc_bucket_count(SIZE_MAX / 2 + 1) == 0, "Near-overflow gives 0 buckets");
}

static void test_ht_missing_key_lookup(void) {
    hashtable_t *ht = ht_create(10);
    TEST_ASSERT(ht != NULL, "Created hash table");

    /* Missing key on empty table */
    TEST_ASSERT(ht_lookup(ht, 42) == NULL, "Lookup 42 on empty table returns NULL");
    TEST_ASSERT(ht_lookup(ht, 0) == NULL, "Lookup 0 on empty table returns NULL");

    /* Insert one key and check other missing keys */
    dlist_node_t *n1 = dlist_node_create(42);
    TEST_ASSERT(ht_insert(ht, n1), "Inserted key 42");

    TEST_ASSERT(ht_lookup(ht, 42) == n1, "Lookup 42 returns n1");
    TEST_ASSERT(ht_lookup(ht, 43) == NULL, "Lookup 43 returns NULL");
    TEST_ASSERT(ht_lookup(ht, 0) == NULL, "Lookup 0 returns NULL");
    TEST_ASSERT(ht_lookup(ht, 1000) == NULL, "Lookup 1000 returns NULL");

    /* Detach key via ht_remove and verify it is now missing */
    dlist_node_t *removed = ht_remove(ht, 42);
    TEST_ASSERT(removed == n1, "Removed key 42");
    TEST_ASSERT(ht_lookup(ht, 42) == NULL, "Lookup 42 after removal returns NULL");

    /* Caller frees the detached node */
    TEST_ASSERT(dlist_node_free(removed), "Freed detached node");
    ht_free(ht);
}

static void test_ht_duplicate_and_repeated_insertions(void) {
    hashtable_t *ht = ht_create(10);
    TEST_ASSERT(ht != NULL, "Created hash table");

    dlist_node_t *node_a = dlist_node_create(100);
    dlist_node_t *node_b = dlist_node_create(100); /* Duplicate key */

    /* 1. First insertion must succeed */
    TEST_ASSERT(ht_insert(ht, node_a), "First insert of key 100 succeeds");
    TEST_ASSERT(ht->size == 1, "Table size is 1");
    TEST_ASSERT(node_a->in_ht, "node_a is marked in_ht");

    /* 2. Inserting the exact same node again must be rejected */
    TEST_ASSERT(!ht_insert(ht, node_a), "Reject repeated insertion of the same node");
    TEST_ASSERT(ht->size == 1, "Size unchanged after repeated node insertion");
    TEST_ASSERT(node_a->hash_next == NULL, "No self-referential cycle created");
    TEST_ASSERT(ht_lookup(ht, 100) == node_a, "Lookup 100 still yields node_a");

    /* 3. Inserting a different node with duplicate key 100 must be rejected */
    TEST_ASSERT(!ht_insert(ht, node_b), "Reject duplicate key 100 from node_b");
    TEST_ASSERT(ht->size == 1, "Size unchanged after duplicate key rejection");
    TEST_ASSERT(!node_b->in_ht, "node_b remains not in hash table");
    TEST_ASSERT(node_b->hash_next == NULL, "node_b hash_next remains NULL");
    TEST_ASSERT(ht_lookup(ht, 100) == node_a, "Lookup 100 still points to node_a");

    /* Free node_b safely since it was never linked */
    TEST_ASSERT(dlist_node_free(node_b), "Unlinked node_b freed safely");

    /* 4. Confirm table remains completely usable after rejected insertions */
    dlist_node_t *node_c = dlist_node_create(200);
    TEST_ASSERT(ht_insert(ht, node_c), "Insert new valid key 200 succeeds after rejections");
    TEST_ASSERT(ht->size == 2, "Size is now 2");
    TEST_ASSERT(ht_lookup(ht, 100) == node_a, "Lookup 100 remains valid");
    TEST_ASSERT(ht_lookup(ht, 200) == node_c, "Lookup 200 is valid");

    /* Clean up: detach via ht_remove then free via dlist_node_free */
    dlist_node_free(ht_remove(ht, 100));
    dlist_node_free(ht_remove(ht, 200));
    ht_free(ht);
}

static void test_ht_repeated_removes(void) {
    hashtable_t *ht = ht_create(10);
    TEST_ASSERT(ht != NULL, "Created hash table");

    dlist_node_t *n1 = dlist_node_create(500);
    TEST_ASSERT(ht_insert(ht, n1), "Inserted key 500");
    TEST_ASSERT(ht->size == 1, "Table size is 1");

    /* First ht_remove detaches and returns node */
    dlist_node_t *r1 = ht_remove(ht, 500);
    TEST_ASSERT(r1 == n1, "First ht_remove returns n1");
    TEST_ASSERT(ht->size == 0, "Table size is 0");
    TEST_ASSERT(!r1->in_ht, "r1->in_ht is false after detach");

    /* Second ht_remove on same key must return NULL and leave size at 0 */
    dlist_node_t *r2 = ht_remove(ht, 500);
    TEST_ASSERT(r2 == NULL, "Repeated ht_remove on 500 returns NULL");
    TEST_ASSERT(ht->size == 0, "Table size remains 0");

    /* Third ht_remove on nonexistent key returns NULL */
    TEST_ASSERT(ht_remove(ht, 9999) == NULL, "ht_remove on missing key returns NULL");

    /* Caller frees detached node */
    TEST_ASSERT(dlist_node_free(r1), "Freed detached node r1");
    ht_free(ht);
}

static void test_ht_collision_chains_and_removals(void) {
    /* Create hash table with 16 buckets */
    hashtable_t *ht = ht_create(4);
    TEST_ASSERT(ht != NULL, "Created 16-bucket table");
    TEST_ASSERT(ht->num_buckets == 16, "Bucket count is 16");

    /*
     * Find 3 keys that genuinely collide in the exact same bucket.
     * We use the exact exported hash function ht_hash_key().
     */
    uint64_t colliding_keys[3];
    int count = 0;
    size_t target_bucket = 0;

    for (uint64_t candidate = 1; count < 3; candidate++) {
        size_t b_idx = (size_t)(ht_hash_key(candidate) & ht->mask);
        if (count == 0) {
            colliding_keys[0] = candidate;
            target_bucket = b_idx;
            count = 1;
        } else if (b_idx == target_bucket) {
            colliding_keys[count++] = candidate;
        }
    }

    /* Verify all 3 keys genuinely map to target_bucket via ht_hash_key() */
    TEST_ASSERT((size_t)(ht_hash_key(colliding_keys[0]) & ht->mask) == target_bucket, "c0 genuinely maps to target_bucket");
    TEST_ASSERT((size_t)(ht_hash_key(colliding_keys[1]) & ht->mask) == target_bucket, "c1 genuinely maps to target_bucket");
    TEST_ASSERT((size_t)(ht_hash_key(colliding_keys[2]) & ht->mask) == target_bucket, "c2 genuinely maps to target_bucket");

    dlist_node_t *c0 = dlist_node_create(colliding_keys[0]);
    dlist_node_t *c1 = dlist_node_create(colliding_keys[1]);
    dlist_node_t *c2 = dlist_node_create(colliding_keys[2]);

    /* Insert order: c0, then c1, then c2 */
    TEST_ASSERT(ht_insert(ht, c0), "Inserted colliding key 0");
    TEST_ASSERT(ht_insert(ht, c1), "Inserted colliding key 1");
    TEST_ASSERT(ht_insert(ht, c2), "Inserted colliding key 2");
    TEST_ASSERT(ht->size == 3, "Table size is 3 with collision chain");

    /*
     * Direct bucket inspection: verify that the bucket chain has length 3
     * with order: c2 (head) -> c1 -> c0 -> NULL
     */
    TEST_ASSERT(ht->buckets[target_bucket] == c2, "Bucket head is c2");
    TEST_ASSERT(c2->hash_next == c1, "c2->hash_next is c1");
    TEST_ASSERT(c1->hash_next == c0, "c1->hash_next is c0");
    TEST_ASSERT(c0->hash_next == NULL, "c0->hash_next is NULL");

    /* Verify all 3 are retrievable via ht_lookup */
    TEST_ASSERT(ht_lookup(ht, colliding_keys[0]) == c0, "Lookup c0");
    TEST_ASSERT(ht_lookup(ht, colliding_keys[1]) == c1, "Lookup c1");
    TEST_ASSERT(ht_lookup(ht, colliding_keys[2]) == c2, "Lookup c2");

    /* Case A: Remove HEAD of collision chain (c2) */
    dlist_node_t *rem_head = ht_remove(ht, colliding_keys[2]);
    TEST_ASSERT(rem_head == c2, "Removed head of collision chain");
    TEST_ASSERT(rem_head->hash_next == NULL, "rem_head hash_next cleared");
    TEST_ASSERT(ht->buckets[target_bucket] == c1, "Bucket head updated to c1");
    TEST_ASSERT(c1->hash_next == c0, "c1->hash_next is still c0");
    TEST_ASSERT(ht->size == 2, "Size is 2 after head removal");
    TEST_ASSERT(ht_lookup(ht, colliding_keys[2]) == NULL, "c2 no longer found");
    TEST_ASSERT(ht_lookup(ht, colliding_keys[1]) == c1, "c1 still found");
    TEST_ASSERT(ht_lookup(ht, colliding_keys[0]) == c0, "c0 still found");
    TEST_ASSERT(dlist_node_free(rem_head), "Freed detached rem_head");

    /* Case B: Re-insert c2 so chain is c2 -> c1 -> c0 again, then remove MIDDLE (c1) */
    dlist_node_t *c2_new = dlist_node_create(colliding_keys[2]);
    TEST_ASSERT(ht_insert(ht, c2_new), "Re-inserted c2_new at bucket head");
    TEST_ASSERT(ht->buckets[target_bucket] == c2_new, "Bucket head is c2_new");
    TEST_ASSERT(c2_new->hash_next == c1, "c2_new points to c1");
    TEST_ASSERT(c1->hash_next == c0, "c1 points to c0");

    dlist_node_t *rem_mid = ht_remove(ht, colliding_keys[1]);
    TEST_ASSERT(rem_mid == c1, "Removed middle of collision chain");
    TEST_ASSERT(rem_mid->hash_next == NULL, "rem_mid hash_next cleared");
    TEST_ASSERT(ht->buckets[target_bucket] == c2_new, "Bucket head is still c2_new");
    TEST_ASSERT(c2_new->hash_next == c0, "c2_new bypassed c1 directly to c0");
    TEST_ASSERT(ht->size == 2, "Size is 2 after middle removal");
    TEST_ASSERT(ht_lookup(ht, colliding_keys[1]) == NULL, "c1 no longer found");
    TEST_ASSERT(ht_lookup(ht, colliding_keys[2]) == c2_new, "c2_new still found");
    TEST_ASSERT(ht_lookup(ht, colliding_keys[0]) == c0, "c0 still found");
    TEST_ASSERT(dlist_node_free(rem_mid), "Freed detached rem_mid");

    /* Case C: Remove TAIL of collision chain (c0) */
    dlist_node_t *rem_tail = ht_remove(ht, colliding_keys[0]);
    TEST_ASSERT(rem_tail == c0, "Removed tail of collision chain");
    TEST_ASSERT(rem_tail->hash_next == NULL, "rem_tail hash_next cleared");
    TEST_ASSERT(ht->buckets[target_bucket] == c2_new, "Bucket head is still c2_new");
    TEST_ASSERT(c2_new->hash_next == NULL, "c2_new is now sole element in bucket");
    TEST_ASSERT(ht->size == 1, "Size is 1 after tail removal");
    TEST_ASSERT(ht_lookup(ht, colliding_keys[0]) == NULL, "c0 no longer found");
    TEST_ASSERT(ht_lookup(ht, colliding_keys[2]) == c2_new, "c2_new still found");
    TEST_ASSERT(dlist_node_free(rem_tail), "Freed detached rem_tail");

    /* Remove final element */
    dlist_node_t *rem_last = ht_remove(ht, colliding_keys[2]);
    TEST_ASSERT(rem_last == c2_new, "Removed final element");
    TEST_ASSERT(ht->buckets[target_bucket] == NULL, "Bucket is now empty");
    TEST_ASSERT(ht->size == 0, "Table is empty");
    TEST_ASSERT(dlist_node_free(rem_last), "Freed final element");

    ht_free(ht);
}

static void test_node_ownership_and_safe_freeing(void) {
    hashtable_t *ht = ht_create(10);
    dlist_t list;
    dlist_init(&list);

    dlist_node_t *node = dlist_node_create(777);

    /* 1. Insert node into both list and hash table (standard cache entry state) */
    TEST_ASSERT(dlist_insert_head(&list, node), "Inserted into dlist");
    TEST_ASSERT(ht_insert(ht, node), "Inserted into hashtable");
    TEST_ASSERT(node->in_list, "node->in_list is true");
    TEST_ASSERT(node->in_ht, "node->in_ht is true");

    /* 2. Direct attempt to free node while linked in list & table must be blocked */
    TEST_ASSERT(!dlist_node_free(node), "dlist_node_free blocked when linked in list & ht");

    /* 3. Cache eviction sequence:
     *    Step a: detach from hash table via ht_remove
     */
    dlist_node_t *unlinked_ht = ht_remove(ht, 777);
    TEST_ASSERT(unlinked_ht == node, "Detached from ht");
    TEST_ASSERT(!node->in_ht, "node->in_ht is now false");
    TEST_ASSERT(node->in_list, "node->in_list is still true");

    /* Node is still in list, so freeing must STILL be blocked */
    TEST_ASSERT(!dlist_node_free(node), "dlist_node_free blocked while still in list");

    /*    Step b: detach from dlist */
    dlist_remove(&list, node);
    TEST_ASSERT(!node->in_list, "node->in_list is now false");
    TEST_ASSERT(!node->in_ht, "node->in_ht is false");

    /*    Step c: now that node is completely detached from both, free must succeed */
    TEST_ASSERT(dlist_node_free(node), "dlist_node_free succeeds when fully detached");

    ht_free(ht);
    dlist_free_all(&list);
}

static void test_ht_large_scale_collisions(void) {
    /* 16 buckets with 64 elements to verify stability under heavy collisions */
    hashtable_t *ht = ht_create(4);
    TEST_ASSERT(ht != NULL, "Created hash table");

    const size_t num_keys = 64;
    dlist_node_t *nodes[64];

    for (size_t i = 0; i < num_keys; i++) {
        nodes[i] = dlist_node_create(i * 100 + 7);
        TEST_ASSERT(ht_insert(ht, nodes[i]), "Insert into collision table");
    }

    TEST_ASSERT(ht->size == num_keys, "All 64 keys inserted");

    /* Verify all 64 keys are retrievable */
    for (size_t i = 0; i < num_keys; i++) {
        dlist_node_t *found = ht_lookup(ht, i * 100 + 7);
        TEST_ASSERT(found == nodes[i], "Collision lookup matches");
    }

    /* Detach and free half the keys */
    for (size_t i = 0; i < num_keys; i += 2) {
        dlist_node_t *rem = ht_remove(ht, i * 100 + 7);
        TEST_ASSERT(rem == nodes[i], "Detached even-index key");
        TEST_ASSERT(dlist_node_free(rem), "Freed detached key");
    }

    TEST_ASSERT(ht->size == num_keys / 2, "Size is 32 after removing 32 keys");

    /* Verify removed keys are absent, while untouched keys remain intact */
    for (size_t i = 0; i < num_keys; i++) {
        dlist_node_t *found = ht_lookup(ht, i * 100 + 7);
        if (i % 2 == 0) {
            TEST_ASSERT(found == NULL, "Removed key returns NULL");
        } else {
            TEST_ASSERT(found == nodes[i], "Remaining key still retrievable");
        }
    }

    /* Detach and free remaining keys */
    for (size_t i = 1; i < num_keys; i += 2) {
        dlist_node_t *rem = ht_remove(ht, i * 100 + 7);
        TEST_ASSERT(dlist_node_free(rem), "Freed remaining key");
    }

    TEST_ASSERT(ht->size == 0, "All nodes removed");
    ht_free(ht);
}

int main(void) {
    printf("=========================================\n");
    printf(" Running Milestone 2 Data Structure Tests\n");
    printf("=========================================\n\n");

    test_structure_sizes();

    printf("[TEST] Doubly-Linked List Primitives:\n");
    test_dlist_empty();
    printf("  -> Empty list tests passed.\n");
    test_dlist_single_element();
    printf("  -> Single element (C=1) tests passed.\n");
    test_dlist_multi_element_and_removals();
    printf("  -> Head/tail/middle removal tests passed.\n");
    test_dlist_move_to_head();
    printf("  -> Move to head tests passed.\n\n");

    printf("[TEST] Fixed-Bucket Hash Table:\n");
    test_ht_capacity_and_bucket_sizing();
    printf("  -> Capacity validation & power-of-two sizing passed.\n");
    test_ht_missing_key_lookup();
    printf("  -> Missing key lookup tests passed.\n");
    test_ht_duplicate_and_repeated_insertions();
    printf("  -> Duplicate keys & repeated node insertions rejected successfully.\n");
    test_ht_repeated_removes();
    printf("  -> Repeated removes edge cases passed.\n");
    test_ht_collision_chains_and_removals();
    printf("  -> Exact hash collision verification & chain removals passed.\n");
    test_node_ownership_and_safe_freeing();
    printf("  -> Node ownership & safe detachment invariants verified.\n");
    test_ht_large_scale_collisions();
    printf("  -> Heavy collision chains passed.\n\n");

    printf("=========================================\n");
    printf(" ALL TESTS PASSED: %d / %d assertions OK\n", tests_passed, tests_run);
    printf("=========================================\n");

    return 0;
}
