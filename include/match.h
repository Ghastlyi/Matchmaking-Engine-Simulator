#ifndef MATCH_H
#define MATCH_H

#include <time.h>
#include "common.h"

typedef enum {
    MATCH_ACTIVE,
    MATCH_COMPLETED
} MatchStatus;

typedef struct {
    int match_id;
    int player1_id;
    int player2_id;
    Region server_region;
    double compatibility_score;
    MatchStatus status;
    time_t creation_time;
    time_t completion_time;
    int winner_id;
    int p1_rating_change;
    int p2_rating_change;
} Match;

typedef struct {
    Match *data;
    size_t size;
    size_t capacity;
} MatchHistory;

void match_init(Match *m, int id, int p1_id, int p2_id, Region region, double score);
void matchhistory_init(MatchHistory *mh);
void matchhistory_destroy(MatchHistory *mh);
void matchhistory_add(MatchHistory *mh, Match m);
void match_print(const Match *m);
void matchhistory_print_active(const MatchHistory *mh);
void matchhistory_print_all(const MatchHistory *mh);

#endif
