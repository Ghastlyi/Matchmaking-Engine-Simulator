#ifndef PRIORITYQUEUE_H
#define PRIORITYQUEUE_H

#include <time.h>
#include <stdlib.h>

typedef struct {
    int player_id;
    time_t join_time;   // earlier time = higher priority
} PQNode;

typedef struct {
    PQNode *nodes;
    size_t size;
    size_t capacity;
} PriorityQueue;

void pq_init(PriorityQueue *pq, size_t capacity);
void pq_destroy(PriorityQueue *pq);
int pq_insert(PriorityQueue *pq, int player_id, time_t join_time);
PQNode pq_extract_min(PriorityQueue *pq);
PQNode pq_peek(const PriorityQueue *pq);
int pq_is_empty(const PriorityQueue *pq);
int pq_remove_by_id(PriorityQueue *pq, int player_id);
size_t pq_size(const PriorityQueue *pq);

#endif
