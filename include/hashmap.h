#ifndef HASHMAP_H
#define HASHMAP_H
#include <stdio.h>
#include <stdlib.h>
typedef struct HashNode{
   int pid;
   size_t playerIndex;
   struct HashNode *next;
}HashNode;
typedef struct{
   HashNode **buckets;
   size_t bucket_count;
   size_t size;
}HashMap;

void hashmap_init(HashMap *map, size_t bucket_count);
void hashmap_destroy(HashMap *map);
int hashmap_insert(HashMap *map,int player_id,size_t player_index);
HashNode *hashmap_search(HashMap *map,int player_id);
int hashmap_delete(HashMap *map,int player_id);
static size_t hash(int player_id, size_t bucket_count);
#endif