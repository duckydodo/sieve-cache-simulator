#include "dlist.h"
#include <stdlib.h>

void dlist_init(dlist_t *list) {
    if (!list) return;
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

dlist_node_t *dlist_node_create(uint64_t key) {
    dlist_node_t *node = malloc(sizeof(*node));
    if (!node) return NULL;
    node->key = key;
    node->visited = false;
    node->in_list = false;
    node->in_ht = false;
    node->prev = NULL;
    node->next = NULL;
    node->hash_next = NULL;
    return node;
}

bool dlist_node_free(dlist_node_t *node) {
    if (!node) {
        return false;
    }
    /* Enforce ownership invariant: never free a node linked in list or hash table */
    if (node->in_list || node->in_ht) {
        return false;
    }
    free(node);
    return true;
}

bool dlist_insert_head(dlist_t *list, dlist_node_t *node) {
    if (!list || !node || node->in_list) {
        return false;
    }

    node->prev = NULL;
    node->next = list->head;

    if (list->head != NULL) {
        list->head->prev = node;
    } else {
        list->tail = node;
    }

    list->head = node;
    node->in_list = true;
    list->size++;

    return true;
}

void dlist_remove(dlist_t *list, dlist_node_t *node) {
    if (!list || !node || !node->in_list) {
        return;
    }

    if (node->prev != NULL) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }

    if (node->next != NULL) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    node->prev = NULL;
    node->next = NULL;
    node->in_list = false;

    if (list->size > 0) {
        list->size--;
    }
}

void dlist_move_to_head(dlist_t *list, dlist_node_t *node) {
    if (!list || !node || !node->in_list || list->head == node) {
        return;
    }

    dlist_remove(list, node);
    dlist_insert_head(list, node);
}

void dlist_free_all(dlist_t *list) {
    if (!list) return;

    dlist_node_t *curr = list->head;
    while (curr != NULL) {
        dlist_node_t *next = curr->next;
        curr->prev = NULL;
        curr->next = NULL;
        curr->in_list = false;
        curr = next;
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}
