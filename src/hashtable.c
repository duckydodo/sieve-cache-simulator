#include "hashtable.h"
#include <stdlib.h>

/*
 * 64-bit integer mixing hash function (MurmurHash3 fmix64).
 * Produces excellent bit avalanche across the entire 64-bit range.
 */
uint64_t ht_hash_key(uint64_t key) {
    key ^= key >> 33;
    key *= UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 33;
    key *= UINT64_C(0xc4ceb9fe1a85ec53);
    key ^= key >> 33;
    return key;
}

size_t ht_calc_bucket_count(size_t capacity) {
    if (capacity == 0) {
        return 0;
    }

    /* Overflow check: capacity * 2 must not overflow size_t */
    if (capacity > SIZE_MAX / 2) {
        return 0;
    }

    size_t target = capacity * 2;
    if (target < 16) {
        target = 16;
    }

    size_t b = 16;
    while (b < target) {
        if (b > SIZE_MAX / 2) {
            return 0; /* Next shift would overflow */
        }
        b <<= 1;
    }

    return b;
}

hashtable_t *ht_create(size_t capacity) {
    size_t num_buckets = ht_calc_bucket_count(capacity);
    if (num_buckets == 0) {
        return NULL;
    }

    hashtable_t *ht = malloc(sizeof(*ht));
    if (!ht) {
        return NULL;
    }

    ht->buckets = calloc(num_buckets, sizeof(dlist_node_t *));
    if (!ht->buckets) {
        free(ht);
        return NULL;
    }

    ht->num_buckets = num_buckets;
    ht->mask = num_buckets - 1;
    ht->size = 0;

    return ht;
}

dlist_node_t *ht_lookup(const hashtable_t *ht, uint64_t key) {
    if (!ht || !ht->buckets) {
        return NULL;
    }

    size_t idx = ht_hash_key(key) & ht->mask;
    dlist_node_t *curr = ht->buckets[idx];

    while (curr != NULL) {
        if (curr->key == key) {
            return curr;
        }
        curr = curr->hash_next;
    }

    return NULL;
}

bool ht_insert(hashtable_t *ht, dlist_node_t *node) {
    if (!ht || !ht->buckets || !node) {
        return false;
    }

    /* Reject if node is already linked in a hash table or has lingering pointer */
    if (node->in_ht || node->hash_next != NULL) {
        return false;
    }

    /* Reject duplicate keys (expected O(1) bucket scan) */
    if (ht_lookup(ht, node->key) != NULL) {
        return false;
    }

    size_t idx = ht_hash_key(node->key) & ht->mask;
    node->hash_next = ht->buckets[idx];
    ht->buckets[idx] = node;
    node->in_ht = true;
    ht->size++;

    return true;
}

dlist_node_t *ht_remove(hashtable_t *ht, uint64_t key) {
    if (!ht || !ht->buckets) {
        return NULL;
    }

    size_t idx = ht_hash_key(key) & ht->mask;
    dlist_node_t **curr = &ht->buckets[idx];

    while (*curr != NULL) {
        if ((*curr)->key == key) {
            dlist_node_t *found = *curr;
            *curr = found->hash_next;
            found->hash_next = NULL;
            found->in_ht = false;
            ht->size--;
            return found;
        }
        curr = &((*curr)->hash_next);
    }

    return NULL;
}

void ht_free(hashtable_t *ht) {
    if (!ht) {
        return;
    }

    if (ht->buckets) {
        for (size_t i = 0; i < ht->num_buckets; i++) {
            dlist_node_t *curr = ht->buckets[i];
            while (curr != NULL) {
                dlist_node_t *next = curr->hash_next;
                curr->hash_next = NULL;
                curr->in_ht = false;
                curr = next;
            }
        }
        free(ht->buckets);
    }

    free(ht);
}
