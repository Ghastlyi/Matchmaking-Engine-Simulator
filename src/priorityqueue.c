#include "priorityqueue.h"
#include <stdio.h>

void pq_init(PriorityQueue *pq, size_t capacity) {
    pq->capacity = capacity;
    pq->size = 0;
    pq->nodes = (PQNode *)malloc(capacity * sizeof(PQNode));
}

void pq_destroy(PriorityQueue *pq) {
    free(pq->nodes);
    pq->nodes = NULL;
    pq->size = 0;
    pq->capacity = 0;
}

static void swap(PQNode *a, PQNode *b) {
    PQNode temp = *a;
    *a = *b;
    *b = temp;
}

static void sift_up(PriorityQueue *pq, size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (pq->nodes[idx].join_time < pq->nodes[parent].join_time) {
            swap(&pq->nodes[idx], &pq->nodes[parent]);
            idx = parent;
        } else {
            break;
        }
    }
}

static void sift_down(PriorityQueue *pq, size_t idx) {
    while (1) {
        size_t left = 2 * idx + 1;
        size_t right = 2 * idx + 2;
        size_t smallest = idx;

        if (left < pq->size && pq->nodes[left].join_time < pq->nodes[smallest].join_time) {
            smallest = left;
        }
        if (right < pq->size && pq->nodes[right].join_time < pq->nodes[smallest].join_time) {
            smallest = right;
        }

        if (smallest != idx) {
            swap(&pq->nodes[idx], &pq->nodes[smallest]);
            idx = smallest;
        } else {
            break;
        }
    }
}

int pq_insert(PriorityQueue *pq, int player_id, time_t join_time) {
    if (pq->size == pq->capacity) {
        pq->capacity *= 2;
        PQNode *temp = (PQNode *)realloc(pq->nodes, pq->capacity * sizeof(PQNode));
        if (!temp) return 0;
        pq->nodes = temp;
    }
    pq->nodes[pq->size].player_id = player_id;
    pq->nodes[pq->size].join_time = join_time;
    sift_up(pq, pq->size);
    pq->size++;
    return 1;
}

PQNode pq_extract_min(PriorityQueue *pq) {
    PQNode min_node = { -1, 0 };
    if (pq->size == 0) return min_node;

    min_node = pq->nodes[0];
    pq->nodes[0] = pq->nodes[pq->size - 1];
    pq->size--;
    if (pq->size > 0) {
        sift_down(pq, 0);
    }
    return min_node;
}

PQNode pq_peek(const PriorityQueue *pq) {
    if (pq->size == 0) {
        PQNode empty = { -1, 0 };
        return empty;
    }
    return pq->nodes[0];
}

int pq_is_empty(const PriorityQueue *pq) {
    return pq->size == 0;
}

int pq_remove_by_id(PriorityQueue *pq, int player_id) {
    for (size_t i = 0; i < pq->size; ++i) {
        if (pq->nodes[i].player_id == player_id) {
            pq->nodes[i] = pq->nodes[pq->size - 1];
            pq->size--;
            if (i < pq->size) {
                sift_up(pq, i);
                sift_down(pq, i);
            }
            return 1;
        }
    }
    return 0;
}

size_t pq_size(const PriorityQueue *pq) {
    return pq->size;
}
