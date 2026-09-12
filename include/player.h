#ifndef PLAYER_H
#define PLAYER_H
#include <time.h>
#include "common.h"
#include "hashmap.h"

//This is player type
typedef struct{
   int id;
   char name[MAX_NM_LEN];
   int rating;
   int ping;
   Region region;
   PlayerStatus status;
   int wins;
   int losses;
   time_t queuetime;
}Player;
//This is collection of players in one structure
typedef struct{
   Player *data;
   size_t size;  //number of players
   size_t capacity; //number of slots of players allocated
   HashMap index;
} PlayerDatabase;

void playerInit(Player *player,int id,const char *name,int rating,int ping,Region region,PlayerStatus status,int wins,int losses,time_t queuetime);
void playerPrint(const Player *player);
void player_dbinit(PlayerDatabase *db);
void player_dbdestroy(PlayerDatabase *db);
void player_dbadd(PlayerDatabase *db, Player player);
Player *player_dbget(PlayerDatabase *db, size_t index);
Player registerPlayer();
void player_dbPrint(const PlayerDatabase *db);
void profileprint(Player *player);
#endif