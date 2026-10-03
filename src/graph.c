#include "graph.h"
#include <stdio.h>

void graph_init(CompatibilityGraph *g, size_t capacity) {
    g->capacity = capacity;
    g->size = 0;
    g->nodes = (GraphNode *)malloc(capacity * sizeof(GraphNode));
}

void graph_destroy(CompatibilityGraph *g) {
    graph_clear(g);
    free(g->nodes);
    g->nodes = NULL;
    g->capacity = 0;
}

int graph_add_vertex(CompatibilityGraph *g, int player_id) {
    if (graph_find_vertex_index(g, player_id) != -1) return 0; // Already exists

    if (g->size == g->capacity) {
        g->capacity = g->capacity == 0 ? 4 : g->capacity * 2;
        GraphNode *temp = (GraphNode *)realloc(g->nodes, g->capacity * sizeof(GraphNode));
        if (!temp) return 0;
        g->nodes = temp;
    }
    
    g->nodes[g->size].player_id = player_id;
    g->nodes[g->size].head = NULL;
    g->size++;
    return 1;
}

int graph_add_edge(CompatibilityGraph *g, int src_id, int dest_id, double weight) {
    int src_idx = graph_find_vertex_index(g, src_id);
    int dest_idx = graph_find_vertex_index(g, dest_id);
    
    if (src_idx == -1 || dest_idx == -1) return 0;

    // Add to src
    Edge *e1 = (Edge *)malloc(sizeof(Edge));
    e1->dest_player_id = dest_id;
    e1->weight = weight;
    e1->next = g->nodes[src_idx].head;
    g->nodes[src_idx].head = e1;

    // Add to dest (undirected)
    Edge *e2 = (Edge *)malloc(sizeof(Edge));
    e2->dest_player_id = src_id;
    e2->weight = weight;
    e2->next = g->nodes[dest_idx].head;
    g->nodes[dest_idx].head = e2;

    return 1;
}

int graph_find_vertex_index(const CompatibilityGraph *g, int player_id) {
    for (size_t i = 0; i < g->size; i++) {
        if (g->nodes[i].player_id == player_id) {
            return (int)i;
        }
    }
    return -1;
}

Edge *graph_get_edges(const CompatibilityGraph *g, int player_id) {
    int idx = graph_find_vertex_index(g, player_id);
    if (idx != -1) {
        return g->nodes[idx].head;
    }
    return NULL;
}

int graph_find_best_match(const CompatibilityGraph *g, int player_id) {
    int idx = graph_find_vertex_index(g, player_id);
    if (idx == -1) return -1;

    Edge *curr = g->nodes[idx].head;
    int best_match = -1;
    double best_weight = -1.0;

    while (curr != NULL) {
        if (best_match == -1 || curr->weight < best_weight) {
            best_match = curr->dest_player_id;
            best_weight = curr->weight;
        }
        curr = curr->next;
    }
    return best_match;
}

void graph_remove_vertex(CompatibilityGraph *g, int player_id) {
    int idx = graph_find_vertex_index(g, player_id);
    if (idx == -1) return;

    // Remove all outgoing edges from this vertex and corresponding incoming edges
    Edge *curr = g->nodes[idx].head;
    while (curr != NULL) {
        int neighbor_id = curr->dest_player_id;
        int neighbor_idx = graph_find_vertex_index(g, neighbor_id);
        
        if (neighbor_idx != -1) {
            Edge **n_curr_ptr = &g->nodes[neighbor_idx].head;
            while (*n_curr_ptr != NULL) {
                if ((*n_curr_ptr)->dest_player_id == player_id) {
                    Edge *to_free = *n_curr_ptr;
                    *n_curr_ptr = (*n_curr_ptr)->next;
                    free(to_free);
                    break;
                }
                n_curr_ptr = &(*n_curr_ptr)->next;
            }
        }
        
        Edge *next = curr->next;
        free(curr);
        curr = next;
    }

    g->nodes[idx] = g->nodes[g->size - 1];
    g->size--;
}

void graph_clear(CompatibilityGraph *g) {
    for (size_t i = 0; i < g->size; i++) {
        Edge *curr = g->nodes[i].head;
        while (curr != NULL) {
            Edge *next = curr->next;
            free(curr);
            curr = next;
        }
    }
    g->size = 0;
}

void graph_print(const CompatibilityGraph *g) {
    for (size_t i = 0; i < g->size; i++) {
        printf("Vertex %d: ", g->nodes[i].player_id);
        Edge *curr = g->nodes[i].head;
        while (curr != NULL) {
            printf("-> (ID: %d, W: %.2f) ", curr->dest_player_id, curr->weight);
            curr = curr->next;
        }
        printf("\n");
    }
}
