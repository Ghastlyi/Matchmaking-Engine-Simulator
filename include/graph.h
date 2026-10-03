#ifndef GRAPH_H
#define GRAPH_H

#include <stdlib.h>

typedef struct Edge {
    int dest_player_id;
    double weight;      // compatibility score (lower = better)
    struct Edge *next;
} Edge;

typedef struct {
    int player_id;
    Edge *head;         // adjacency list
} GraphNode;

typedef struct {
    GraphNode *nodes;
    size_t size;
    size_t capacity;
} CompatibilityGraph;

void graph_init(CompatibilityGraph *g, size_t capacity);
void graph_destroy(CompatibilityGraph *g);
int graph_add_vertex(CompatibilityGraph *g, int player_id);
int graph_add_edge(CompatibilityGraph *g, int src_id, int dest_id, double weight);
int graph_find_vertex_index(const CompatibilityGraph *g, int player_id);
Edge *graph_get_edges(const CompatibilityGraph *g, int player_id);
int graph_find_best_match(const CompatibilityGraph *g, int player_id);
void graph_remove_vertex(CompatibilityGraph *g, int player_id);
void graph_clear(CompatibilityGraph *g);
void graph_print(const CompatibilityGraph *g);

#endif
