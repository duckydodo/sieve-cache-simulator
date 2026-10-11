#include "cache.h"

/*
 * SIEVE Eviction Policy (NSDI '24 - Zhang et al.):
 *
 * Algorithm 1:
 * - Cache hit: Sets node->visited = true ("lazy promotion", zero pointer mutations).
 * - Cache miss:
 *     1. Examines node pointed by hand (starts at tail if hand == NULL).
 *     2. While node->visited == true:
 *          node->visited = false;
 *          node = node->prev;
 *          if node == NULL then node = tail; // wraparound to tail
 *     3. When an unvisited victim (visited == false) is reached:
 *          hand = victim->prev;              // Retain next hand position BEFORE discard
 *          return victim;                    // Caller detaches and frees victim
 *
 * Complexity:
 *   - Cache hit: O(1) bit update.
 *   - Eviction: O(C) worst-case scan where C is cache capacity.
 */

static void sieve_on_hit(cache_t *cache, dlist_node_t *node) {
    (void)cache;
    if (node != NULL) {
        node->visited = true;
    }
}

static dlist_node_t *sieve_evict(cache_t *cache) {
    if (!cache || cache->list.size == 0) {
        return NULL;
    }

    /* Start from current hand position, or tail if hand is NULL */
    dlist_node_t *obj = cache->hand;
    if (obj == NULL) {
        obj = cache->list.tail;
    }

    /*
     * Eviction scan: worst-case complexity is O(C) where C is cache capacity.
     * Scans from tail towards head via prev, clearing visited bits.
     */
    while (obj->visited) {
        obj->visited = false;
        obj = obj->prev;
        if (obj == NULL) {
            obj = cache->list.tail; /* Wraparound */
        }
    }

    /*
     * Crucial invariant from Algorithm 1 (Line 13):
     * Retain next hand position (obj->prev) BEFORE the caller unlinks/frees obj.
     * If obj was head (or sole node), obj->prev is NULL, so hand becomes NULL.
     * On the next eviction, hand == NULL will wrap back to the tail.
     */
    cache->hand = obj->prev;

    return obj;
}

const cache_policy_ops_t sieve_ops = {
    .name = "SIEVE",
    .on_hit = sieve_on_hit,
    .evict = sieve_evict
};
