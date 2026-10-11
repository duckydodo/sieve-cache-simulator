#include "cache.h"

/*
 * LRU (Least Recently Used) Eviction Policy:
 * - Cache hit: moves the accessed entry to the head of the queue (MRU position).
 * - Eviction: evicts the least recently used entry at the tail of the queue.
 */

static void lru_on_hit(cache_t *cache, dlist_node_t *node) {
    if (!cache || !node) {
        return;
    }
    dlist_move_to_head(&cache->list, node);
}

static dlist_node_t *lru_evict(cache_t *cache) {
    if (!cache || cache->list.size == 0) {
        return NULL;
    }
    /* The tail node represents the least recently used entry */
    return cache->list.tail;
}

const cache_policy_ops_t lru_ops = {
    .name = "LRU",
    .on_hit = lru_on_hit,
    .evict = lru_evict
};
