#include <stdio.h>
#include <stdlib.h>
#include "hashmap.h"


static size_t hash(int player_id, size_t bucket_count)
{
   return (size_t)player_id % bucket_count;
}
void hashmap_init(HashMap *map, size_t bucket_count){
   if (map == NULL || bucket_count == 0){
      return;
   }
   map->buckets = malloc(bucket_count * sizeof(HashNode *));
   if (map->buckets == NULL){
      printf("HashMap allocation failed.\n");
      map->bucket_count = 0;
      map->size = 0;
      return;
   }
   map->bucket_count = bucket_count;
   map->size = 0;
   for (size_t i = 0; i < bucket_count; i++){
      map->buckets[i] = NULL;
   }
}
void hashmap_destroy(HashMap *map){
   if (map == NULL || map->buckets == NULL){
      return;
   }
   for (size_t i = 0; i < map->bucket_count; i++){
      HashNode *current = map->buckets[i];
      while (current != NULL){
         HashNode *next = current->next;
         free(current);
         current = next;
      }
   }
   free(map->buckets);
   map->buckets = NULL;
   map->bucket_count = 0;
   map->size = 0;
}

int hashmap_insert(HashMap *map, int player_id, size_t player_index){
   if (map == NULL ||map->buckets == NULL ||map->bucket_count == 0){
      return 0;
   }
   if (hashmap_search(map, player_id) != NULL){
      return 0;
   }
   size_t bucket = hash(player_id, map->bucket_count);
   HashNode *newNode = malloc(sizeof(HashNode));
   if (newNode == NULL){
      return 0;
   }
   newNode->pid = player_id;
   newNode->playerIndex = player_index;
   newNode->next = map->buckets[bucket];
   map->buckets[bucket] = newNode;
   map->size++;
   return 1;
}

HashNode *hashmap_search(HashMap *map, int player_id){
   if (map == NULL ||map->buckets == NULL ||map->bucket_count == 0){
      return NULL;
   }
   size_t bucket = hash(player_id, map->bucket_count);
   HashNode *current = map->buckets[bucket];
   while (current != NULL){
      if (current->pid == player_id){
         return current;
      }
      current = current->next;
   }
   return NULL;
}

int hashmap_delete(HashMap *map, int player_id){
   if (map == NULL || map->buckets == NULL ||map->bucket_count == 0){
      return 0;
   }
   size_t bucket = hash(player_id, map->bucket_count);
   HashNode *current = map->buckets[bucket];
   HashNode *previous = NULL;
   while (current != NULL)
   {
      if (current->pid == player_id)
      {
         if (previous == NULL){
            map->buckets[bucket] = current->next;
         }
         else{
            previous->next = current->next;
         }
         free(current);
         map->size--;
         return 1;
      }
      previous = current;
      current = current->next;
   }
   return 0;
}