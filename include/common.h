#ifndef COMMON_H
#define COMMON_H

#define MAX_NM_LEN 50
#define MAX_SER 100

typedef enum
{
   availablePlayers,
   waitingPlayers,
   inmatchPlayers,
   inactivePlayers
}PlayerStatus;

typedef enum
{
   REG_IND,
   REG_SIG,
   REG_EU,
   REG_NA
}Region;

#endif