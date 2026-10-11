#ifndef DLIST_H
#define DLIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Cache node in the doubly linked list.
 *
 * Queue Orientation (agreed convention from NSDI '24 paper):
 *   - list->head: Newest / MRU end (insertion point).
 *   - list->tail: Oldest / LRU end.
 *   - node->prev: Points towards list->head (newer items).
 *   - node->next: Points towards list->tail (older items).
 *
 * Intrusive pointers and ownership flags:
 *   - node->hash_next: Collision chain pointer for the hash table bucket.
 *   - node->in_list: True if currently linked in a dlist_t.
 *   - node->in_ht: True if currently linked in a hashtable_t.
 *
 * Ownership Contract:
 *   The cache manager exclusively owns all dlist_node_t allocations.
 *   Neither dlist_t nor hashtable_t owns node memory.
 *   A node must NEVER be freed while linked in a list (in_list == true)
 *   or hash table (in_ht == true). dlist_node_free() strictly enforces
 *   this invariant.
 */
typedef struct dlist_node {
    uint64_t key;
    bool visited;
    bool in_list;
    bool in_ht;
    struct dlist_node *prev;
    struct dlist_node *next;
    struct dlist_node *hash_next;
} dlist_node_t;

typedef struct dlist {
    dlist_node_t *head;
    dlist_node_t *tail;
    size_t size;
} dlist_t;

/* Initialize an empty doubly linked list */
void dlist_init(dlist_t *list);

/* Allocate and initialize a single list node (unlinked) */
dlist_node_t *dlist_node_create(uint64_t key);

/*
 * Free a detached list node.
 * Returns true if freed. Returns false and refuses to free if the node
 * is still linked in a list (in_list == true) or hash table (in_ht == true).
 */
bool dlist_node_free(dlist_node_t *node);

/*
 * Insert a node at the head (newest/MRU position).
 * Rejects insertion if node is already linked in a list (node->in_list == true).
 * Returns true on success, false on invalid inputs or rejection.
 */
bool dlist_insert_head(dlist_t *list, dlist_node_t *node);

/* Unlink a node from the list without freeing it */
void dlist_remove(dlist_t *list, dlist_node_t *node);

/* Move an existing node in the list to the head */
void dlist_move_to_head(dlist_t *list, dlist_node_t *node);

/*
 * Detach all nodes from the list without freeing their memory.
 * Clears in_list, prev, and next on all nodes and resets the list to empty.
 * Node deallocation remains the responsibility of the owner via dlist_node_free().
 */
void dlist_free_all(dlist_t *list);

#endif /* DLIST_H */
