#ifndef SERVER_H
#define SERVER_H

#include "common.h"

typedef struct {
    int server_id;
    char name[MAX_NM_LEN];
    Region region;
    int capacity;        // max concurrent matches
    int current_load;    // current active matches
    int total_served;    // total matches hosted
    int base_ping;       // base latency for this server
} Server;

typedef struct {
    Server servers[MAX_SERVERS];
    size_t count;
} ServerPool;

void server_init(Server *s, int id, const char *name, Region region, int capacity, int base_ping);
void serverpool_init(ServerPool *pool);
void server_print(const Server *s);
void serverpool_print(const ServerPool *pool);
int serverpool_find_best(const ServerPool *pool, Region preferred_region, int player_ping);
void serverpool_add_load(ServerPool *pool, int server_index);
void serverpool_remove_load(ServerPool *pool, int server_index);
extern const char *regionToString(Region region);

#endif
