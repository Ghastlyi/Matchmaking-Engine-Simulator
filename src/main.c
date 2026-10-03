#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "matchmaking.h"
#include "menu.h"

int main(void) {
    srand((unsigned int)time(NULL));

    MatchmakingSystem sys;
    mms_init(&sys);

    /* Load persistent data from .dat files (silently succeeds or fails) */
    mms_load_players(&sys, "data/players.dat");
    mms_load_matches(&sys, "data/matches.dat");
    mms_load_servers(&sys, "data/servers.dat");

    showMenu(&sys);

    mms_destroy(&sys);
    return 0;
}