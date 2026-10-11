#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dlist.h"
#include "hashtable.h"

/* Forward declaration of cache_t */
typedef struct cache cache_t;

/* Supported cache eviction policy types */
typedef enum cache_policy_type {
    CACHE_POLICY_FIFO,
    CACHE_POLICY_LRU,
    CACHE_POLICY_SIEVE
} cache_policy_type_t;

/* Cache execution statistics */
typedef struct cache_stats {
    uint64_t accesses;   /* Total access requests */
    uint64_t hits;       /* Cache hits */
    uint64_t misses;     /* Cache misses */
    uint64_t evictions;  /* Number of objects evicted */
} cache_stats_t;

/*
 * Policy dispatch operations (Linux kernel style ops table).
 * Minimal interface for hit handling and eviction candidate selection.
 */
typedef struct cache_policy_ops {
    const char *name;
    void (*on_hit)(cache_t *cache, dlist_node_t *node);
    dlist_node_t *(*evict)(cache_t *cache);
} cache_policy_ops_t;

/*
 * Cache structure (user-space cache simulator).
 *
 * Ownership:
 *   The cache manager exclusively owns all dlist_node_t allocations and deallocations.
 *   The doubly linked list and hash table are non-owning indexing structures.
 */
struct cache {
    size_t capacity;
    cache_policy_type_t policy_type;
    const cache_policy_ops_t *ops;
    dlist_t list;
    hashtable_t *ht;
    dlist_node_t *hand; /* Eviction hand pointer used by SIEVE */
    cache_stats_t stats;
};

/* Exported policy operations */
extern const cache_policy_ops_t fifo_ops;
extern const cache_policy_ops_t lru_ops;
extern const cache_policy_ops_t sieve_ops;

/*
 * Create a new cache with the specified capacity and eviction policy.
 * Returns NULL on invalid capacity (0) or memory allocation failure.
 */
cache_t *cache_create(size_t capacity, cache_policy_type_t policy_type);

/*
 * Destroy the cache, safely freeing all nodes, internal data structures,
 * and the cache manager itself.
 */
void cache_destroy(cache_t *cache);

/*
 * Access a key in the cache.
 * Returns true on cache HIT (node updated according to policy ops).
 * Returns false on cache MISS (inserts key at head, evicts victim if full).
 */
bool cache_access(cache_t *cache, uint64_t key);

/*
 * Query whether a key exists in the cache without updating stats or policy state.
 */
bool cache_contains(const cache_t *cache, uint64_t key);

/* Query current number of items cached */
size_t cache_size(const cache_t *cache);

/* Query maximum capacity */
size_t cache_capacity(const cache_t *cache);

/* Read statistics */
const cache_stats_t *cache_get_stats(const cache_t *cache);

/* Reset statistics */
void cache_reset_stats(cache_t *cache);

/* Return human-readable policy name string */
const char *cache_policy_name(cache_policy_type_t policy_type);

#endif /* CACHE_H */
