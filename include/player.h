#ifndef PLAYER_H
#define PLAYER_H
#include <time.h>
#include "common.h"
typedef struct{
   int id;
   char name[MAX_NM_LEN];
   int rating;
   int ping;
   Region region;
   PlayerStatus status;
   int wins;
   int looses;
   time_t queuetime;
}Player;

void playerInit(Player *player,int id,const char *name,int rating,int ping,Region region);
void playerPrint(const Player *player);
#endif