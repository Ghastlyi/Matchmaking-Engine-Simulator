#include <stdio.h>
#include <stdlib.h>
#include "match.h"
#include "player.h" // Needed for regionToString if not declared elsewhere, or just declare it

extern const char *regionToString(Region region);

void match_init(Match *m, int id, int p1_id, int p2_id, Region region, double score) {
    m->match_id = id;
    m->player1_id = p1_id;
    m->player2_id = p2_id;
    m->server_region = region;
    m->compatibility_score = score;
    m->status = MATCH_ACTIVE;
    m->creation_time = time(NULL);
    m->completion_time = 0;
    m->winner_id = 0;
    m->p1_rating_change = 0;
    m->p2_rating_change = 0;
}

void matchhistory_init(MatchHistory *mh) {
    mh->capacity = 16;
    mh->size = 0;
    mh->data = (Match *)malloc(mh->capacity * sizeof(Match));
    if (!mh->data) {
        printf("Memory allocation failed for match history.\n");
    }
}

void matchhistory_destroy(MatchHistory *mh) {
    free(mh->data);
    mh->data = NULL;
    mh->size = 0;
    mh->capacity = 0;
}

void matchhistory_add(MatchHistory *mh, Match m) {
    if (mh->size == mh->capacity) {
        mh->capacity *= 2;
        Match *temp = (Match *)realloc(mh->data, mh->capacity * sizeof(Match));
        if (!temp) {
            printf("Memory reallocation failed for match history.\n");
            return;
        }
        mh->data = temp;
    }
    mh->data[mh->size++] = m;
}

void match_print(const Match *m) {
    printf(" #%-4d | %-5d vs %-5d | %-6s | %6.4f | %s\n",
           m->match_id, m->player1_id, m->player2_id, regionToString(m->server_region),
           m->compatibility_score, (m->status == MATCH_ACTIVE) ? "ACTIVE" : "DONE");
}

void matchhistory_print_active(const MatchHistory *mh) {
    printf("\n------------------------------------------------------------\n");
    printf("                      ACTIVE MATCHES\n");
    printf("------------------------------------------------------------\n");
    size_t active_count = 0;
    for (size_t i = 0; i < mh->size; ++i) {
        if (mh->data[i].status == MATCH_ACTIVE) active_count++;
    }

    if (active_count == 0) {
        printf(" No active matches.\n");
        printf("------------------------------------------------------------\n");
        return;
    }

    printf(" ID    | Competitors   | Region | Score  | Status\n");
    printf("-------+---------------+--------+--------+---------\n");
    for (size_t i = 0; i < mh->size; ++i) {
        if (mh->data[i].status == MATCH_ACTIVE) {
            match_print(&mh->data[i]);
        }
    }
    printf("------------------------------------------------------------\n");
    printf(" Total active: %zu match(es)\n", active_count);
    printf("------------------------------------------------------------\n");
}

void matchhistory_print_all(const MatchHistory *mh) {
    printf("\n------------------------------------------------------------\n");
    printf("                      MATCH HISTORY\n");
    printf("------------------------------------------------------------\n");
    if (mh->size == 0) {
        printf(" No matches recorded.\n");
        printf("------------------------------------------------------------\n");
        return;
    }
    printf(" ID    | Competitors   | Region | Score  | Status\n");
    printf("-------+---------------+--------+--------+---------\n");
    for (size_t i = 0; i < mh->size; ++i) {
        match_print(&mh->data[i]);
    }
    printf("------------------------------------------------------------\n");
    printf(" Total: %zu match(es)\n", mh->size);
    printf("------------------------------------------------------------\n");
}
