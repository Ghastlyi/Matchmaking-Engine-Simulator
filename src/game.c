#include <stdlib.h>
#include <math.h>
#include "game.h"

/*
 * logic(p1, p2) simulates a match outcome between p1 and p2.
 * Returns 1 if p1 wins, 2 if p2 wins.
 * Uses Elo-based expected score and relative win rates with fairness clamping.
 */
int logic(Player *p1, Player *p2) {
    if (!p1 || !p2) return 1;

    double rating_adv = (double)(p1->rating - p2->rating) / 400.0;
    double p1_expected = 1.0 / (1.0 + pow(10.0, -rating_adv));

    int t1 = p1->wins + p1->losses;
    int t2 = p2->wins + p2->losses;
    double wr1 = (t1 > 0) ? (double)p1->wins / (double)t1 : 0.5;
    double wr2 = (t2 > 0) ? (double)p2->wins / (double)t2 : 0.5;

    double wr_relative = (wr1 + wr2 > 0.0) ? wr1 / (wr1 + wr2) : 0.5;
    double p1_prob = 0.7 * p1_expected + 0.3 * wr_relative;

    if (p1_prob < 0.15) p1_prob = 0.15;
    if (p1_prob > 0.85) p1_prob = 0.85;

    double roll = (double)rand() / (double)RAND_MAX;
    return (roll < p1_prob) ? 1 : 2;
}
