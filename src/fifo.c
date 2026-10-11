#include "cache.h"

/*
 * FIFO (First-In, First-Out) Eviction Policy:
 * - Cache hit: leaves node order unchanged.
 * - Eviction: evicts the oldest inserted entry at the tail of the queue.
 */

static void fifo_on_hit(cache_t *cache, dlist_node_t *node) {
    (void)cache;
    (void)node;
    /* FIFO performs no queue reordering on cache hits */
}

static dlist_node_t *fifo_evict(cache_t *cache) {
    if (!cache || cache->list.size == 0) {
        return NULL;
    }
    /* The tail node represents the oldest inserted entry */
    return cache->list.tail;
}

const cache_policy_ops_t fifo_ops = {
    .name = "FIFO",
    .on_hit = fifo_on_hit,
    .evict = fifo_evict
};
