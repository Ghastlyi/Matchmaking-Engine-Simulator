#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "player.h"

const char *regionToString(Region region)
{
   switch (region)
   {
   case REG_IND:
      return "IND";
   case REG_SIG:
      return "SIG";
   case REG_EU:
      return "EU";
   case REG_NA:
      return "NA";
   default:
      return "UNKNOWN";
   }
}
void playerInit(Player *player, int id, const char *name, int rating, int ping, Region region,PlayerStatus status,int wins,int losses,time_t queuetime){
   player->id=id;
   strcpy(player->name,name);
   player->rating=rating;
   player->ping=ping;
   player->region=region;
   player->status=status;
   player->wins=wins;
   player->losses=losses;
   player->queuetime=queuetime;
}
void playerPrint(const Player *player){
   printf("\n%s's id:%d\n",player->name,player->id);
   printf("%s's rating:%d\n",player->name,player->rating);
   printf("%s's ping:%d\n",player->name,player->ping);
   printf("%s's region:%s\n", player->name,regionToString(player->region));
}
Player registerPlayer(){
   int id=(int)time(NULL);
   char name[50];
   int rating=1500;
   int ping,servercode;
   printf("\nEnter player name:");
   scanf(" %49s",name);
   printf("Select player region:\n1.India\n2.Singapore\n3.Europe\n4.North America\nEnter choice:");
   do{
      scanf(" %d",&servercode);
      if(servercode<1 || servercode>4){
         printf("\nInvalid choice please enter 1-4\n");
      }
   }while(servercode<1 || servercode>4);
   Region region = (Region)(servercode-1);
   printf("Enter the ping of the player:");
   scanf("%d",&ping);
   PlayerStatus status = 0;
   Player p;
   playerInit(&p,id,name,rating,ping,region,status,0,0,0);
   return p;
}

void player_dbinit(PlayerDatabase *db){
   db->capacity = 4;
   db->size = 0;
   db->data = malloc(db->capacity * sizeof(Player));
   if (db->data == NULL)
   {
      printf("Memory allocation failed.\n");
      return;
   }
   hashmap_init(&db->index, 16);
}

void player_dbadd(PlayerDatabase *db,Player player){
   if (db->size == db->capacity){
      db->capacity *= 2;
      Player *temp = realloc(db->data,db->capacity * sizeof(Player));
      if (temp == NULL){
         printf("Memory allocation failed.\n");
         return;
      }
      db->data = temp;
   }
   db->data[db->size] = player;
   db->size++;
   size_t index = db->size;
   db->data[index] = player;
   if (!hashmap_insert(&db->index, player.id, index)){
      printf("Failed to insert player into HashMap.\n");
      return;
   }
   db->size++;
}

void player_dbdestroy(PlayerDatabase *db){
   hashmap_destroy(&db->index);
   free(db->data);
   db->data = NULL;
   db->size = 0;
   db->capacity = 0;
}

void player_dbPrint(const PlayerDatabase *db){
   printf("\nPlayers in database: %zu\n", db->size);
   printf("Database capacity: %zu\n\n", db->capacity);
   for (size_t i = 0; i < db->size; i++){
      printf("Player %zu:\n", i);
      playerPrint(player_dbget((PlayerDatabase *)db,i));
      printf("--------------------------\n");
   }
}

Player *player_dbget(PlayerDatabase *db, size_t index){
   if (index >= db->size)
      return NULL;
   return &db->data[index];
}

void profileprint(Player *player){
   printf("\n========== PLAYER PROFILE ==========\n");
   printf("Name      : %s\n", player->name);
   printf("ID        : %d\n", player->id);
   printf("Rating    : %d\n", player->rating);
   printf("Ping      : %d\n", player->ping);
   printf("Region    : %s\n", regionToString(player->region));
   printf("Wins      : %d\n", player->wins);
   printf("Losses    : %d\n", player->losses);
   printf("====================================\n");
}