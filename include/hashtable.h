#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dlist.h"

/*
 * Fixed-bucket hash table with separate chaining.
 *
 * Performance Characteristics:
 *   - Lookup: Expected O(1) assuming a uniform hash distribution.
 *   - Insert: Expected O(1).
 *   - Remove: Expected O(1).
 *   (Not guaranteed O(1) in the worst case if hash collisions degenerate).
 *
 * Bucket count is fixed at initialization to a power of 2:
 *   num_buckets >= 2 * capacity AND num_buckets >= 16.
 * This bounds the average load factor to <= 0.5 under normal cache capacity.
 *
 * Node Ownership Model:
 *   The hash table is strictly a non-owning indexing data structure that
 *   references nodes (dlist_node_t) owned exclusively by the cache manager.
 *   - The hash table NEVER allocates or frees dlist_node_t nodes.
 *   - ht_insert() links a node into its bucket chain and sets node->in_ht = true.
 *     It rejects duplicate keys and rejects inserting a node already in a table.
 *   - ht_remove() detaches a node from its bucket chain, clears node->hash_next = NULL,
 *     clears node->in_ht = false, and returns the detached node without freeing it.
 *   - Freeing node memory is the caller's responsibility via dlist_node_free(),
 *     which can only be called once the node is detached from both list and table.
 *   - ht_free() frees only the hash table struct and bucket array, clearing in_ht
 *     and hash_next on any remaining nodes without freeing the nodes themselves.
 */
typedef struct hashtable {
    dlist_node_t **buckets;
    size_t num_buckets; /* Always a power of 2 */
    size_t mask;        /* num_buckets - 1 (for fast bitwise modulo) */
    size_t size;        /* Current number of entries stored in the table */
} hashtable_t;

/*
 * 64-bit integer mixing hash function (MurmurHash3 fmix64).
 * Exposed so tests and callers can verify and inspect exact bucket mapping.
 */
uint64_t ht_hash_key(uint64_t key);

/*
 * Calculate power-of-two bucket count for a given cache capacity.
 * Returns 0 if capacity is 0 or if calculating the power of 2 would overflow size_t.
 */
size_t ht_calc_bucket_count(size_t capacity);

/*
 * Create a new hash table sized for the specified cache capacity.
 * Returns NULL on invalid capacity (0 or overflow) or allocation failure.
 */
hashtable_t *ht_create(size_t capacity);

/*
 * Look up a node by key. Expected O(1).
 * Returns pointer to dlist_node_t if found, NULL otherwise.
 */
dlist_node_t *ht_lookup(const hashtable_t *ht, uint64_t key);

/*
 * Insert a node into the hash table. Expected O(1).
 *
 * Rejection conditions:
 *   - Returns false if ht or node is NULL.
 *   - Returns false if node is already linked in a hash table (node->in_ht == true
 *     or node->hash_next != NULL). Prevents duplicate node insertion & cycles.
 *   - Returns false if a node with the same key already exists in the table.
 *
 * In all rejection cases, the table and all collision chains remain completely unchanged.
 * Returns true on successful insertion.
 */
bool ht_insert(hashtable_t *ht, dlist_node_t *node);

/*
 * Remove and detach a node with the given key from the hash table without freeing it.
 * Unlinks the node from its bucket chain, clears node->hash_next = NULL,
 * sets node->in_ht = false, and returns the node.
 * Returns pointer to the unlinked dlist_node_t, or NULL if not found.
 */
dlist_node_t *ht_remove(hashtable_t *ht, uint64_t key);

/*
 * Free the hash table structure and its bucket array.
 * Clears in_ht and hash_next on any remaining nodes without freeing them.
 */
void ht_free(hashtable_t *ht);

#endif /* HASHTABLE_H */
