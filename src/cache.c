#include "cache.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

cache_t *cache_create(size_t capacity, cache_policy_type_t policy_type) {
    if (capacity == 0) {
        return NULL;
    }

    const cache_policy_ops_t *ops = NULL;
    switch (policy_type) {
        case CACHE_POLICY_FIFO:
            ops = &fifo_ops;
            break;
        case CACHE_POLICY_LRU:
            ops = &lru_ops;
            break;
        case CACHE_POLICY_SIEVE:
            ops = &sieve_ops;
            break;
        default:
            return NULL;
    }

    cache_t *cache = malloc(sizeof(*cache));
    if (!cache) {
        return NULL;
    }

    cache->ht = ht_create(capacity);
    if (!cache->ht) {
        free(cache);
        return NULL;
    }

    dlist_init(&cache->list);
    cache->capacity = capacity;
    cache->policy_type = policy_type;
    cache->ops = ops;
    cache->hand = NULL;
    memset(&cache->stats, 0, sizeof(cache->stats));

    return cache;
}

void cache_destroy(cache_t *cache) {
    if (!cache) {
        return;
    }

    /*
     * Ownership deallocation pattern:
     * 1. Detach all nodes from the hash table without freeing them.
     * 2. Free the hash table structure and bucket array.
     * 3. Iterate through list nodes, detach each from list, and free via dlist_node_free.
     * 4. Free the cache structure.
     */
    if (cache->ht) {
        ht_free(cache->ht);
        cache->ht = NULL;
    }

    dlist_node_t *curr = cache->list.head;
    while (curr != NULL) {
        dlist_node_t *next = curr->next;
        dlist_remove(&cache->list, curr);
        dlist_node_free(curr);
        curr = next;
    }

    free(cache);
}

bool cache_access(cache_t *cache, uint64_t key) {
    if (!cache) {
        return false;
    }

    cache->stats.accesses++;

    /* 1. Hash table lookup: expected O(1) */
    dlist_node_t *node = ht_lookup(cache->ht, key);
    if (node != NULL) {
        /* Cache HIT */
        cache->stats.hits++;
        if (cache->ops && cache->ops->on_hit) {
            cache->ops->on_hit(cache, node);
        }
        return true;
    }

    /* Cache MISS */
    cache->stats.misses++;

    /* 2. Evict victim if cache is full */
    if (cache->list.size >= cache->capacity) {
        if (!cache->ops || !cache->ops->evict) {
            return false;
        }

        dlist_node_t *victim = cache->ops->evict(cache);
        if (victim != NULL) {
            /* Detach victim from hash table */
            dlist_node_t *v_ht = ht_remove(cache->ht, victim->key);
            assert(v_ht == victim);

            /* Detach victim from doubly linked list */
            dlist_remove(&cache->list, victim);

            /* Safely free victim (now completely detached from both structures) */
            bool freed = dlist_node_free(victim);
            assert(freed);

            cache->stats.evictions++;
        }
    }

    /* 3. Allocate and insert new cache node at head */
    dlist_node_t *new_node = dlist_node_create(key);
    if (!new_node) {
        return false;
    }

    if (!dlist_insert_head(&cache->list, new_node)) {
        dlist_node_free(new_node);
        return false;
    }

    if (!ht_insert(cache->ht, new_node)) {
        dlist_remove(&cache->list, new_node);
        dlist_node_free(new_node);
        return false;
    }

    return false;
}

bool cache_contains(const cache_t *cache, uint64_t key) {
    if (!cache || !cache->ht) {
        return false;
    }
    return ht_lookup(cache->ht, key) != NULL;
}

size_t cache_size(const cache_t *cache) {
    return cache ? cache->list.size : 0;
}

size_t cache_capacity(const cache_t *cache) {
    return cache ? cache->capacity : 0;
}

const cache_stats_t *cache_get_stats(const cache_t *cache) {
    return cache ? &cache->stats : NULL;
}

void cache_reset_stats(cache_t *cache) {
    if (cache) {
        memset(&cache->stats, 0, sizeof(cache->stats));
    }
}

const char *cache_policy_name(cache_policy_type_t policy_type) {
    switch (policy_type) {
        case CACHE_POLICY_FIFO:  return "FIFO";
        case CACHE_POLICY_LRU:   return "LRU";
        case CACHE_POLICY_SIEVE: return "SIEVE";
        default:                 return "UNKNOWN";
    }
}
