#include <stdio.h>
#include <string.h>
#include "server.h"

void server_init(Server *s, int id, const char *name, Region region, int capacity, int base_ping) {
    s->server_id = id;
    strncpy(s->name, name, MAX_NM_LEN - 1);
    s->name[MAX_NM_LEN - 1] = '\0';
    s->region = region;
    s->capacity = capacity;
    s->current_load = 0;
    s->total_served = 0;
    s->base_ping = base_ping;
}

void serverpool_init(ServerPool *pool) {
    pool->count = 4;
    server_init(&pool->servers[0], 0, "India-Central", REG_IND, 50, 20);
    server_init(&pool->servers[1], 1, "Singapore-East", REG_SIG, 50, 30);
    server_init(&pool->servers[2], 2, "Europe-West", REG_EU, 50, 25);
    server_init(&pool->servers[3], 3, "NA-East", REG_NA, 50, 15);
}

void server_print(const Server *s) {
    printf(" [%d] %s (%s) | Load: %d/%d | Ping: %dms\n",
           s->server_id, s->name, regionToString(s->region),
           s->current_load, s->capacity, s->base_ping);
}

void serverpool_print(const ServerPool *pool) {
    printf("\n------------------------------------------------------------\n");
    printf("                        SERVER POOL\n");
    printf("------------------------------------------------------------\n");
    printf(" ID | Server Name      | Region | Load   | Ping | Served\n");
    printf("----+------------------+--------+--------+------+-----------\n");
    for (size_t i = 0; i < pool->count; ++i) {
        const Server *s = &pool->servers[i];
        printf(" %2d | %-16s | %-6s | %2d/%-2d | %2dms | %6d\n",
               s->server_id, s->name, regionToString(s->region),
               s->current_load, s->capacity, s->base_ping, s->total_served);
    }
    printf("------------------------------------------------------------\n");
}

int serverpool_find_best(const ServerPool *pool, Region preferred_region, int player_ping) {
    (void)player_ping;  /* reserved for future latency-based selection */
    int best_idx = -1;
    int min_load = -1;

    for (size_t i = 0; i < pool->count; ++i) {
        if (pool->servers[i].region == preferred_region && pool->servers[i].current_load < pool->servers[i].capacity) {
            if (best_idx == -1 || pool->servers[i].current_load < min_load) {
                best_idx = i;
                min_load = pool->servers[i].current_load;
            }
        }
    }

    if (best_idx != -1) return best_idx;

    for (size_t i = 0; i < pool->count; ++i) {
        if (pool->servers[i].current_load < pool->servers[i].capacity) {
            if (best_idx == -1 || pool->servers[i].current_load < min_load) {
                best_idx = i;
                min_load = pool->servers[i].current_load;
            }
        }
    }

    return best_idx;
}

void serverpool_add_load(ServerPool *pool, int server_index) {
    if (server_index >= 0 && server_index < (int)pool->count) {
        pool->servers[server_index].current_load++;
        pool->servers[server_index].total_served++;
    }
}

void serverpool_remove_load(ServerPool *pool, int server_index) {
    if (server_index >= 0 && server_index < (int)pool->count) {
        if (pool->servers[server_index].current_load > 0) {
            pool->servers[server_index].current_load--;
        }
    }
}
