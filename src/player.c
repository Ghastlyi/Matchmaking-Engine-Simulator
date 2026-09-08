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
void playerInit(Player *player, int id, const char *name, int rating, int ping, Region region){
   player->id=id;
   strcpy(player->name,name);
   player->rating=rating;
   player->ping=ping;
   player->region=region;
}
void playerPrint(const Player *player){
   printf("\n%s's id:%d\n",player->name,player->id);
   printf("%s's rating:%d\n",player->name,player->rating);
   printf("%s's ping:%d\n",player->name,player->ping);
   printf("%s's region:%s\n", player->name,regionToString(player->region));
}
