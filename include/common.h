#ifndef COMMON_H
#define COMMON_H

#define MAX_NM_LEN 50
#define MAX_SER 100
#define NUM_REGIONS 4
#define INITIAL_DB_CAPACITY 4
#define INITIAL_BUCKET_COUNT 16
#define MAX_MATCHES 1000
#define MAX_SERVERS 16
#define MAX_QUEUE_SIZE 10000

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