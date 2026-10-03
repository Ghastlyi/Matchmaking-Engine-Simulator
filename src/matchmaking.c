/*
 * matchmaking.c  –  Core matchmaking engine with dynamic weights
 *
 * This file implements:
 *   - Static and dynamic weight computation
 *   - Pool statistics collection
 *   - Compatibility score calculation
 *   - Eligibility filtering
 *   - Priority-queue-driven matchmaking with graph
 *   - Probabilistic match outcomes (Elo-inspired)
 *   - Binary persistence for players, matches, servers
 *   - Simulation and experimental comparison
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <direct.h>   /* _mkdir on Windows */
#include "matchmaking.h"

extern const char *regionToString(Region region);

/* ============================================================
 *  SYSTEM LIFECYCLE
 * ============================================================ */

void mms_init(MatchmakingSystem *sys) {
    player_dbinit(&sys->players);
    matchhistory_init(&sys->matches);
    serverpool_init(&sys->servers);
    pq_init(&sys->waiting_queue, 64);
    memset(&sys->stats, 0, sizeof(MatchmakingStats));
    sys->use_dynamic_weights = MMS_WEIGHT_CALIBRATED;  /* Calibrated dynamic by default */
    sys->next_match_id = 1;
    sys->verbose = 1;
}

void mms_destroy(MatchmakingSystem *sys) {
    player_dbdestroy(&sys->players);
    matchhistory_destroy(&sys->matches);
    pq_destroy(&sys->waiting_queue);
}

/* ============================================================
 *  QUEUE OPERATIONS
 * ============================================================ */

int mms_join_queue(MatchmakingSystem *sys, int player_id) {
    HashNode *node = hashmap_search(&sys->players.index, player_id);
    if (node == NULL) {
        printf("Player %d not found in database.\n", player_id);
        return 0;
    }
    Player *p = player_dbget(&sys->players, node->playerIndex);
    if (p->status == waitingPlayers) {
        printf("Player %s is already in the queue.\n", p->name);
        return 0;
    }
    if (p->status == inmatchPlayers) {
        printf("Player %s is currently in a match.\n", p->name);
        return 0;
    }

    p->status = waitingPlayers;
    p->queuetime = time(NULL);
    pq_insert(&sys->waiting_queue, player_id, p->queuetime);
    printf("Player %s (ID:%d) joined matchmaking queue.\n", p->name, p->id);
    return 1;
}

int mms_leave_queue(MatchmakingSystem *sys, int player_id) {
    HashNode *node = hashmap_search(&sys->players.index, player_id);
    if (node == NULL) {
        printf("Player %d not found.\n", player_id);
        return 0;
    }
    Player *p = player_dbget(&sys->players, node->playerIndex);
    if (p->status != waitingPlayers) {
        printf("Player %s is not in the queue.\n", p->name);
        return 0;
    }

    pq_remove_by_id(&sys->waiting_queue, player_id);
    p->status = availablePlayers;
    p->queuetime = 0;
    printf("Player %s left the matchmaking queue.\n", p->name);
    return 1;
}

/* ============================================================
 *  STATIC WEIGHTS
 * ============================================================ */

MatchWeights compute_static_weights(void) {
    MatchWeights w;
    w.w_rating  = 0.35;
    w.w_ping    = 0.20;
    w.w_region  = 0.20;
    w.w_winrate = 0.10;
    w.w_wait    = 0.15;
    return w;
}

/* ============================================================
 *  POOL STATISTICS (for dynamic weight computation)
 * ============================================================ */

PoolStats compute_pool_stats(PlayerDatabase *db, PriorityQueue *pq) {
    PoolStats ps;
    memset(&ps, 0, sizeof(PoolStats));

    double now = (double)time(NULL);
    size_t n = pq->size;
    ps.total_waiting = n;

    if (n == 0) return ps;

    /* First pass: means */
    double sum_rating = 0, sum_ping = 0, sum_wr = 0, sum_wait = 0;

    for (size_t i = 0; i < n; i++) {
        int pid = pq->nodes[i].player_id;
        HashNode *hn = hashmap_search(&db->index, pid);
        if (!hn) continue;
        Player *p = player_dbget(db, hn->playerIndex);

        sum_rating += p->rating;
        sum_ping   += p->ping;

        int total = p->wins + p->losses;
        double wr = (total > 0) ? (double)p->wins / total : 0.5;
        sum_wr += wr;

        double wt = now - (double)p->queuetime;
        if (wt < 0) wt = 0;
        sum_wait += wt;

        ps.region_counts[p->region]++;
    }

    ps.avg_rating  = sum_rating / n;
    ps.avg_ping    = sum_ping   / n;
    ps.avg_winrate = sum_wr     / n;
    ps.avg_wait_time = sum_wait / n;

    /* Second pass: standard deviations */
    double var_rating = 0, var_ping = 0, var_wr = 0;

    for (size_t i = 0; i < n; i++) {
        int pid = pq->nodes[i].player_id;
        HashNode *hn = hashmap_search(&db->index, pid);
        if (!hn) continue;
        Player *p = player_dbget(db, hn->playerIndex);

        double dr = p->rating - ps.avg_rating;
        var_rating += dr * dr;

        double dp = p->ping - ps.avg_ping;
        var_ping += dp * dp;

        int total = p->wins + p->losses;
        double wr = (total > 0) ? (double)p->wins / total : 0.5;
        double dw = wr - ps.avg_winrate;
        var_wr += dw * dw;
    }

    ps.rating_stddev  = sqrt(var_rating / n);
    ps.ping_stddev    = sqrt(var_ping / n);
    ps.winrate_stddev = sqrt(var_wr / n);

    return ps;
}

/* ============================================================
 *  DYNAMIC WEIGHT COMPUTATION
 *
 *  Key idea: High spread/variation in a factor means the pool
 *  is diverse on that dimension — harder to match tightly — so
 *  we REDUCE that factor's weight to avoid penalising players
 *  unfairly.  Low spread means everyone is similar on that
 *  dimension — easy to match — so we can INCREASE its weight.
 *
 *  Wait pressure works inversely: the longer people wait, the
 *  MORE important the wait factor becomes.
 *
 *  All weights are clamped to [0.05, 0.60] and normalized to 1.0.
 * ============================================================ */

MatchWeights compute_dynamic_weights(PlayerDatabase *db, PriorityQueue *pq) {
    PoolStats ps = compute_pool_stats(db, pq);

    MatchWeights base = compute_static_weights();

    if (ps.total_waiting < 2) return base;

    /* Coefficient of variation (spread relative to mean) */
    double rating_spread = (ps.avg_rating > 1.0)
        ? ps.rating_stddev / ps.avg_rating : 0.0;
    double ping_spread = (ps.avg_ping > 1.0)
        ? ps.ping_stddev / ps.avg_ping : 0.0;
    double winrate_spread = (ps.avg_winrate > 0.01)
        ? ps.winrate_stddev / ps.avg_winrate : 0.0;

    /* Region imbalance: 1 - (min/max) among non-zero region counts */
    int minr = 999999, maxr = 0;
    for (int r = 0; r < NUM_REGIONS; r++) {
        if (ps.region_counts[r] > 0) {
            if (ps.region_counts[r] < minr) minr = ps.region_counts[r];
            if (ps.region_counts[r] > maxr) maxr = ps.region_counts[r];
        }
    }
    double region_imbalance = (maxr > 0) ? 1.0 - (double)minr / maxr : 0.0;

    /* Wait pressure: normalized to 120s (2 min) */
    double wait_pressure = ps.avg_wait_time / 120.0;
    if (wait_pressure > 1.0) wait_pressure = 1.0;

    /* Adjust base weights:
     * - High spread → reduce weight (hard to match on that axis)
     * - High wait pressure → increase wait weight
     */
    double raw_rating  = base.w_rating  * (1.0 - 0.5 * rating_spread);
    double raw_ping    = base.w_ping    * (1.0 - 0.5 * ping_spread);
    double raw_region  = base.w_region  * (1.0 - 0.5 * region_imbalance);
    double raw_winrate = base.w_winrate * (1.0 - 0.5 * winrate_spread);
    double raw_wait    = base.w_wait    * (1.0 + 2.0 * wait_pressure);

    /* Clamp each to [0.05, 0.60] */
#define CLAMP_W(v) ((v) < 0.05 ? 0.05 : ((v) > 0.60 ? 0.60 : (v)))
    raw_rating  = CLAMP_W(raw_rating);
    raw_ping    = CLAMP_W(raw_ping);
    raw_region  = CLAMP_W(raw_region);
    raw_winrate = CLAMP_W(raw_winrate);
    raw_wait    = CLAMP_W(raw_wait);
#undef CLAMP_W

    /* Small-pool boost: if very few players, push wait weight up */
    if (ps.total_waiting < 6) {
        raw_wait += 0.10;
    }

    /* Normalize to sum = 1.0 */
    double sum = raw_rating + raw_ping + raw_region + raw_winrate + raw_wait;

    MatchWeights w;
    w.w_rating  = raw_rating  / sum;
    w.w_ping    = raw_ping    / sum;
    w.w_region  = raw_region  / sum;
    w.w_winrate = raw_winrate / sum;
    w.w_wait    = raw_wait    / sum;

    return w;
}

/* ============================================================
 *  CALIBRATED DYNAMIC WEIGHT COMPUTATION
 *
 *  Key principles:
 *  1. Pure Quality Separation: Waiting time is NOT a quality weight
 *     (w_wait = 0.0). Waiting time acts as a search window expansion
 *     signal via candidate scarcity, never corrupting quality ranking.
 *  2. Bound Enforcement: Rating weight is strictly protected
 *     [CALIB_MIN_RATING_WEIGHT, CALIB_MAX_RATING_WEIGHT], ensuring skill
 *     parity remains paramount.
 *  3. Dynamic Fine-Tuning: Adjusts weights subtly based on pool
 *     variance without cannibalizing rating or ping.
 * ============================================================ */

MatchWeights compute_calibrated_weights(PlayerDatabase *db, PriorityQueue *pq) {
    PoolStats ps = compute_pool_stats(db, pq);
    MatchWeights w;

    if (ps.total_waiting < 2) {
        w.w_rating  = CALIB_BASE_RATING_WEIGHT;
        w.w_ping    = CALIB_BASE_PING_WEIGHT;
        w.w_region  = CALIB_BASE_REGION_WEIGHT;
        w.w_winrate = CALIB_BASE_WINRATE_WEIGHT;
        w.w_wait    = 0.0;
        return w;
    }

    double rating_cv = (ps.avg_rating > 1.0)
        ? ps.rating_stddev / ps.avg_rating : 0.0;
    double ping_cv = (ps.avg_ping > 1.0)
        ? ps.ping_stddev / ps.avg_ping : 0.0;

    int minr = 999999, maxr = 0;
    for (int r = 0; r < NUM_REGIONS; r++) {
        if (ps.region_counts[r] > 0) {
            if (ps.region_counts[r] < minr) minr = ps.region_counts[r];
            if (ps.region_counts[r] > maxr) maxr = ps.region_counts[r];
        }
    }
    double region_imbalance = (maxr > 0) ? 1.0 - (double)minr / maxr : 0.0;

    double raw_rating  = CALIB_BASE_RATING_WEIGHT;
    double raw_ping    = CALIB_BASE_PING_WEIGHT;
    double raw_region  = CALIB_BASE_REGION_WEIGHT;
    double raw_winrate = CALIB_BASE_WINRATE_WEIGHT;

    if (rating_cv > 0.25) {
        raw_rating += 0.05;
    } else if (rating_cv < 0.10) {
        raw_rating -= 0.05;
    }

    if (ping_cv > 0.60) {
        raw_ping += 0.05;
    } else if (ping_cv < 0.20) {
        raw_ping -= 0.05;
    }

    if (region_imbalance > 0.50) {
        raw_region -= 0.05;
    }

    /* Clamp strictly to scientific bounds */
    if (raw_rating < CALIB_MIN_RATING_WEIGHT) raw_rating = CALIB_MIN_RATING_WEIGHT;
    if (raw_rating > CALIB_MAX_RATING_WEIGHT) raw_rating = CALIB_MAX_RATING_WEIGHT;

    if (raw_ping < CALIB_MIN_PING_WEIGHT) raw_ping = CALIB_MIN_PING_WEIGHT;
    if (raw_ping > CALIB_MAX_PING_WEIGHT) raw_ping = CALIB_MAX_PING_WEIGHT;

    if (raw_region < CALIB_MIN_REGION_WEIGHT) raw_region = CALIB_MIN_REGION_WEIGHT;
    if (raw_region > CALIB_MAX_REGION_WEIGHT) raw_region = CALIB_MAX_REGION_WEIGHT;

    if (raw_winrate < CALIB_MIN_WINRATE_WEIGHT) raw_winrate = CALIB_MIN_WINRATE_WEIGHT;
    if (raw_winrate > CALIB_MAX_WINRATE_WEIGHT) raw_winrate = CALIB_MAX_WINRATE_WEIGHT;

    double sum = raw_rating + raw_ping + raw_region + raw_winrate;

    w.w_rating  = raw_rating / sum;
    w.w_ping    = raw_ping / sum;
    w.w_region  = raw_region / sum;
    w.w_winrate = raw_winrate / sum;
    w.w_wait    = 0.0;

    return w;
}

/* ============================================================
 *  ELIGIBILITY CHECK (Legacy & Static)
 *
 *  Before computing the full compatibility score we apply fast
 *  eligibility filters.  The rating window EXPANDS over time
 *  (5 rating points per second of waiting).  Region restriction
 *  is dropped after 60 seconds of waiting.
 * ============================================================ */

int check_eligibility(const Player *a, const Player *b, double current_time) {
    if (a->status != waitingPlayers || b->status != waitingPlayers)
        return 0;
    if (a->id == b->id) return 0;

    double wait_a = current_time - (double)a->queuetime;
    double wait_b = current_time - (double)b->queuetime;
    double max_wait = (wait_a > wait_b) ? wait_a : wait_b;

    /* Rating window expands: base 500 + 5 per second of max wait */
    int rating_window = 500 + (int)(max_wait * 5);
    int rdiff = abs(a->rating - b->rating);
    if (rdiff > rating_window) return 0;

    /* Region: must match unless someone has waited >60s */
    if (a->region != b->region && max_wait < 60.0)
        return 0;

    return 1;
}

/* ============================================================
 *  COMPATIBILITY SCORE (Legacy Dynamic & Static)
 *
 *  LOWER score = BETTER match.
 *  Each factor is normalized to roughly [0, 1].
 *  The wait_bonus is SUBTRACTED so that longer waits make
 *  players appear more compatible (relaxing standards).
 * ============================================================ */

double calculate_compatibility(const Player *a, const Player *b,
                               const MatchWeights *w,
                               double current_time) {
    /* Rating factor: |diff| / 3000 */
    double rating_diff = fabs((double)(a->rating - b->rating)) / 3000.0;

    /* Ping factor: |diff| / 300 */
    double ping_diff = fabs((double)(a->ping - b->ping)) / 300.0;

    /* Region factor: same=0, different=1 */
    double region_factor = (a->region == b->region) ? 0.0 : 1.0;

    /* Win-rate factor */
    int total_a = a->wins + a->losses;
    int total_b = b->wins + b->losses;
    double wr_a = (total_a > 0) ? (double)a->wins / total_a : 0.5;
    double wr_b = (total_b > 0) ? (double)b->wins / total_b : 0.5;
    double winrate_diff = fabs(wr_a - wr_b);

    /* Wait bonus: average wait / 180s, capped at 1.0 */
    double wait_a = current_time - (double)a->queuetime;
    double wait_b = current_time - (double)b->queuetime;
    if (wait_a < 0) wait_a = 0;
    if (wait_b < 0) wait_b = 0;
    double avg_wait = (wait_a + wait_b) / 2.0;
    double wait_bonus = avg_wait / 180.0;
    if (wait_bonus > 1.0) wait_bonus = 1.0;

    /* Weighted sum (wait is subtracted as a bonus) */
    double score = w->w_rating  * rating_diff
                 + w->w_ping    * ping_diff
                 + w->w_region  * region_factor
                 + w->w_winrate * winrate_diff
                 - w->w_wait    * wait_bonus;

    if (score < 0.001) score = 0.001;  /* floor */
    return score;
}

/* ============================================================
 *  PURE QUALITY SCORE (Calibrated Dynamic)
 *
 *  LOWER score = BETTER match.
 *  Normalized factors in [0, 1].
 *  Crucially, wait time is NOT subtracted. This guarantees that:
 *  - Scores do not slam against the 0.001 floor.
 *  - Edge weights reflect true match fairness.
 *  - Greedy matcher selects the closest peer within the expanded window.
 * ============================================================ */

double calculate_quality_score(const Player *a, const Player *b,
                               const MatchWeights *w) {
    double rating_diff = fabs((double)(a->rating - b->rating)) / 3000.0;
    if (rating_diff > 1.0) rating_diff = 1.0;

    double ping_diff = fabs((double)(a->ping - b->ping)) / 300.0;
    if (ping_diff > 1.0) ping_diff = 1.0;

    double region_factor = (a->region == b->region) ? 0.0 : 1.0;

    int total_a = a->wins + a->losses;
    int total_b = b->wins + b->losses;
    double wr_a = (total_a > 0) ? (double)a->wins / (double)total_a : 0.5;
    double wr_b = (total_b > 0) ? (double)b->wins / (double)total_b : 0.5;
    double winrate_diff = fabs(wr_a - wr_b);

    double score = w->w_rating  * rating_diff
                 + w->w_ping    * ping_diff
                 + w->w_region  * region_factor
                 + w->w_winrate * winrate_diff;

    if (score < 0.0001) score = 0.0001;
    return score;
}

/* ============================================================
 *  CANDIDATE SCARCITY EVALUATION
 *
 *  Evaluates candidate availability in the waiting pool for a
 *  specific player.
 * ============================================================ */

PlayerScarcityContext mms_evaluate_player_scarcity(const MatchmakingSystem *sys,
                                                  int player_id,
                                                  double current_time) {
    PlayerScarcityContext ctx;
    memset(&ctx, 0, sizeof(PlayerScarcityContext));
    ctx.player_id = player_id;

    HashNode *hn = hashmap_search((HashMap *)&sys->players.index, player_id);
    if (!hn) return ctx;
    Player *p = player_dbget((PlayerDatabase *)&sys->players, hn->playerIndex);

    double wt = current_time - (double)p->queuetime;
    if (wt < 0.0) wt = 0.0;
    ctx.wait_time_sec = wt;

    size_t qsize = sys->waiting_queue.size;
    for (size_t i = 0; i < qsize; i++) {
        int other_id = sys->waiting_queue.nodes[i].player_id;
        if (other_id == player_id) continue;

        HashNode *other_hn = hashmap_search((HashMap *)&sys->players.index, other_id);
        if (!other_hn) continue;
        Player *other = player_dbget((PlayerDatabase *)&sys->players, other_hn->playerIndex);
        if (other->status != waitingPlayers) continue;

        int rdiff = abs(p->rating - other->rating);
        int pdiff = abs(p->ping - other->ping);
        int same_reg = (p->region == other->region);

        if (same_reg && rdiff <= CALIB_STRICT_RATING_DIFF && pdiff <= CALIB_STRICT_PING_DIFF) {
            ctx.strict_candidates++;
        }

        if (rdiff <= CALIB_MOD_RATING_DIFF && pdiff <= CALIB_MOD_PING_DIFF &&
            (same_reg || pdiff <= 60)) {
            ctx.moderate_candidates++;
        }

        if (rdiff <= CALIB_MAX_RATING_EXPANSION && pdiff <= CALIB_MAX_PING_EXPANSION) {
            ctx.broad_candidates++;
        }
    }

    if (ctx.strict_candidates >= 2) {
        ctx.scarcity_index = 0.0;
    } else if (ctx.strict_candidates == 1) {
        ctx.scarcity_index = 0.25;
    } else if (ctx.moderate_candidates >= 1) {
        ctx.scarcity_index = 0.65;
    } else {
        ctx.scarcity_index = 1.0;
    }

    double wait_pressure = ctx.wait_time_sec / CALIB_MAX_WAIT_TOLERANCE;
    if (wait_pressure > 1.0) wait_pressure = 1.0;

    ctx.relaxation_factor = wait_pressure * (0.15 + 0.85 * ctx.scarcity_index);
    if (ctx.relaxation_factor > 1.0) ctx.relaxation_factor = 1.0;

    ctx.rating_window = CALIB_STRICT_RATING_DIFF +
        (int)(ctx.relaxation_factor * (CALIB_MAX_RATING_EXPANSION - CALIB_STRICT_RATING_DIFF));

    ctx.ping_window = CALIB_STRICT_PING_DIFF +
        (int)(ctx.relaxation_factor * (CALIB_MAX_PING_EXPANSION - CALIB_STRICT_PING_DIFF));

    ctx.allow_cross_region = (ctx.relaxation_factor >= CALIB_CROSS_REGION_RELAX_REQ);

    return ctx;
}

/* ============================================================
 *  CALIBRATED ELIGIBILITY CHECK
 *
 *  Checks pair eligibility based on scarcity-modulated search windows.
 * ============================================================ */

int check_calibrated_eligibility(const Player *a, const Player *b,
                                const PlayerScarcityContext *ctx_a,
                                const PlayerScarcityContext *ctx_b) {
    if (a->status != waitingPlayers || b->status != waitingPlayers) return 0;
    if (a->id == b->id) return 0;

    int eff_rating_win = (ctx_a->rating_window > ctx_b->rating_window)
        ? ctx_a->rating_window : ctx_b->rating_window;
    int eff_ping_win = (ctx_a->ping_window > ctx_b->ping_window)
        ? ctx_a->ping_window : ctx_b->ping_window;

    int rdiff = abs(a->rating - b->rating);
    if (rdiff > eff_rating_win) return 0;

    int pdiff = abs(a->ping - b->ping);
    if (pdiff > eff_ping_win) return 0;

    if (a->region != b->region) {
        if (!ctx_a->allow_cross_region && !ctx_b->allow_cross_region)
            return 0;
    }

    return 1;
}

/* ============================================================
 *  RUN MATCHMAKING
 *
 *  1. Compute weights (Calibrated Dynamic, Legacy Dynamic, or Static)
 *  2. Collect waiting player IDs from priority queue
 *  3. Build compatibility graph
 *  4. Greedily match: pick longest-waiting, find best edge,
 *     create match, remove both, repeat
 * ============================================================ */

int mms_run_matchmaking(MatchmakingSystem *sys) {
    size_t qsize = pq_size(&sys->waiting_queue);
    if (qsize < 2) {
        if (sys->verbose) {
            printf("Not enough players in queue (%zu). Need at least 2.\n", qsize);
        }
        return 0;
    }

    if (sys->verbose) {
        printf("\n======= RUNNING MATCHMAKING ENGINE =======\n");
        printf("Players in queue: %zu\n", qsize);
    }

    /* Step 1: Compute weights according to mode */
    MatchWeights weights;
    int is_calibrated = (sys->use_dynamic_weights == MMS_WEIGHT_CALIBRATED);
    int is_legacy     = (sys->use_dynamic_weights == MMS_WEIGHT_LEGACY_DYNAMIC);

    if (is_calibrated) {
        weights = compute_calibrated_weights(&sys->players, &sys->waiting_queue);
        if (sys->verbose) printf("\n[Calibrated Dynamic Weights]\n");
    } else if (is_legacy) {
        weights = compute_dynamic_weights(&sys->players, &sys->waiting_queue);
        if (sys->verbose) printf("\n[Legacy Dynamic Weights]\n");
    } else {
        weights = compute_static_weights();
        if (sys->verbose) printf("\n[Static Weights]\n");
    }
    if (sys->verbose) mms_print_weights(&weights);
    sys->stats.last_weights = weights;

    double now = (double)time(NULL);

    /* Step 2: Collect all waiting player IDs */
    size_t n = sys->waiting_queue.size;
    int *waiting_ids = (int *)malloc(n * sizeof(int));
    if (!waiting_ids) {
        if (sys->verbose) printf("Memory allocation failed.\n");
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        waiting_ids[i] = sys->waiting_queue.nodes[i].player_id;
    }

    PlayerScarcityContext *contexts = NULL;
    if (is_calibrated) {
        contexts = (PlayerScarcityContext *)malloc(n * sizeof(PlayerScarcityContext));
        for (size_t i = 0; i < n; i++) {
            contexts[i] = mms_evaluate_player_scarcity(sys, waiting_ids[i], now);
        }
    }

    /* Step 3: Build compatibility graph */
    CompatibilityGraph graph;
    graph_init(&graph, n);

    for (size_t i = 0; i < n; i++) {
        graph_add_vertex(&graph, waiting_ids[i]);
    }

    size_t edges_added = 0;
    for (size_t i = 0; i < n; i++) {
        HashNode *hn_a = hashmap_search(&sys->players.index, waiting_ids[i]);
        if (!hn_a) continue;
        Player *pa = player_dbget(&sys->players, hn_a->playerIndex);

        for (size_t j = i + 1; j < n; j++) {
            HashNode *hn_b = hashmap_search(&sys->players.index, waiting_ids[j]);
            if (!hn_b) continue;
            Player *pb = player_dbget(&sys->players, hn_b->playerIndex);

            int eligible = 0;
            double score = 0.0;

            if (is_calibrated) {
                eligible = check_calibrated_eligibility(pa, pb, &contexts[i], &contexts[j]);
                if (eligible) {
                    score = calculate_quality_score(pa, pb, &weights);
                }
            } else {
                eligible = check_eligibility(pa, pb, now);
                if (eligible) {
                    score = calculate_compatibility(pa, pb, &weights, now);
                }
            }

            if (eligible) {
                graph_add_edge(&graph, pa->id, pb->id, score);
                edges_added++;
            }
        }
    }

    if (sys->verbose) {
        printf("Compatibility graph: %zu vertices, %zu edges\n", n, edges_added);
    }

    /* Step 4: Greedy matching from priority queue */
    int matches_created = 0;

    PriorityQueue temp_pq;
    pq_init(&temp_pq, n);
    for (size_t i = 0; i < n; i++) {
        HashNode *hn = hashmap_search(&sys->players.index, waiting_ids[i]);
        if (!hn) continue;
        Player *p = player_dbget(&sys->players, hn->playerIndex);
        pq_insert(&temp_pq, p->id, p->queuetime);
    }

    int *matched = (int *)calloc(n, sizeof(int));

    while (!pq_is_empty(&temp_pq)) {
        PQNode top = pq_extract_min(&temp_pq);
        int pid = top.player_id;

        int already = 0;
        for (size_t i = 0; i < n; i++) {
            if (waiting_ids[i] == pid && matched[i]) { already = 1; break; }
        }
        if (already) continue;

        int best_match_id = graph_find_best_match(&graph, pid);
        if (best_match_id < 0) {
            sys->stats.unmatched_players++;
            continue;
        }

        int bm_already = 0;
        for (size_t i = 0; i < n; i++) {
            if (waiting_ids[i] == best_match_id && matched[i]) { bm_already = 1; break; }
        }
        if (bm_already) {
            sys->stats.unmatched_players++;
            continue;
        }

        for (size_t i = 0; i < n; i++) {
            if (waiting_ids[i] == pid) matched[i] = 1;
            if (waiting_ids[i] == best_match_id) matched[i] = 1;
        }

        HashNode *hn1 = hashmap_search(&sys->players.index, pid);
        HashNode *hn2 = hashmap_search(&sys->players.index, best_match_id);
        Player *p1 = player_dbget(&sys->players, hn1->playerIndex);
        Player *p2 = player_dbget(&sys->players, hn2->playerIndex);

        double compat;
        if (is_calibrated) {
            compat = calculate_quality_score(p1, p2, &weights);
        } else {
            compat = calculate_compatibility(p1, p2, &weights, now);
        }

        Region match_region = p1->region;
        if (p1->region != p2->region) {
            match_region = (p1->queuetime <= p2->queuetime)
                ? p1->region : p2->region;
        }
        int server_idx = serverpool_find_best(&sys->servers, match_region, 0);
        if (server_idx >= 0) {
            match_region = sys->servers.servers[server_idx].region;
            serverpool_add_load(&sys->servers, server_idx);
        }

        Match m;
        match_init(&m, sys->next_match_id++, pid, best_match_id, match_region, compat);
        matchhistory_add(&sys->matches, m);

        p1->status = inmatchPlayers;
        p2->status = inmatchPlayers;

        pq_remove_by_id(&sys->waiting_queue, pid);
        pq_remove_by_id(&sys->waiting_queue, best_match_id);

        graph_remove_vertex(&graph, pid);
        graph_remove_vertex(&graph, best_match_id);

        double wt1 = now - (double)p1->queuetime;
        double wt2 = now - (double)p2->queuetime;
        if (wt1 < 0.0) wt1 = 0.0;
        if (wt2 < 0.0) wt2 = 0.0;
        sys->stats.total_wait_time += wt1 + wt2;
        if (wt1 > sys->stats.max_wait_time) sys->stats.max_wait_time = wt1;
        if (wt2 > sys->stats.max_wait_time) sys->stats.max_wait_time = wt2;

        int rdiff = abs(p1->rating - p2->rating);
        int pdiff = abs(p1->ping - p2->ping);
        sys->stats.total_rating_diff += (double)rdiff;
        sys->stats.total_ping_diff += (double)pdiff;
        sys->stats.total_compat_score += compat;
        sys->stats.total_matches_created++;

        if (p1->region != p2->region) {
            sys->stats.total_region_mismatches++;
        }
        if (rdiff > CALIB_STRICT_RATING_DIFF || pdiff > CALIB_STRICT_PING_DIFF) {
            sys->stats.candidate_expansions++;
        }

        matches_created++;
        if (sys->verbose) {
            printf("  Match #%d: %s (R:%d) vs %s (R:%d) | Score: %.4f | Server: %s\n",
                   m.match_id, p1->name, p1->rating, p2->name, p2->rating,
                   compat, regionToString(match_region));
        }
    }

    if (sys->verbose) {
        printf("\nMatches created: %d\n", matches_created);
        printf("Unmatched players remaining: %zu\n", pq_size(&sys->waiting_queue));
        printf("==========================================\n");
    }

    free(waiting_ids);
    free(matched);
    if (contexts) free(contexts);
    pq_destroy(&temp_pq);
    graph_destroy(&graph);

    return matches_created;
}

/* ============================================================
 *  PROBABILISTIC MATCH COMPLETION
 *
 *  Winner is determined using an Elo-inspired expected score
 *  formula combined with historical win-rate.
 *
 *    p1_expected = 1 / (1 + 10^(-(R1-R2)/400))
 *    p1_prob = 0.7 * p1_expected + 0.3 * p1_winrate
 *    Clamped to [0.15, 0.85] for fairness
 *
 *  Rating changes use K-factor = 32.
 * ============================================================ */

void mms_complete_match(MatchmakingSystem *sys, int match_id) {
    Match *m = NULL;
    for (size_t i = 0; i < sys->matches.size; i++) {
        if (sys->matches.data[i].match_id == match_id &&
            sys->matches.data[i].status == MATCH_ACTIVE) {
            m = &sys->matches.data[i];
            break;
        }
    }
    if (!m) {
        printf("Active match %d not found.\n", match_id);
        return;
    }

    HashNode *hn1 = hashmap_search(&sys->players.index, m->player1_id);
    HashNode *hn2 = hashmap_search(&sys->players.index, m->player2_id);
    if (!hn1 || !hn2) {
        printf("Players for match %d not found in database.\n", match_id);
        return;
    }

    Player *p1 = player_dbget(&sys->players, hn1->playerIndex);
    Player *p2 = player_dbget(&sys->players, hn2->playerIndex);

    /* Elo expected score for p1 */
    double rating_adv = (double)(p1->rating - p2->rating) / 400.0;
    double p1_expected = 1.0 / (1.0 + pow(10.0, -rating_adv));

    /* Win-rates */
    int t1 = p1->wins + p1->losses;
    int t2 = p2->wins + p2->losses;
    double wr1 = (t1 > 0) ? (double)p1->wins / (double)t1 : 0.5;
    double wr2 = (t2 > 0) ? (double)p2->wins / (double)t2 : 0.5;

    /* Symmetric relative win-rate factor: equal win rates give 0.5 */
    double wr_relative = (wr1 + wr2 > 0.0) ? wr1 / (wr1 + wr2) : 0.5;

    /* Combined probability: 70% rating-based, 30% winrate-based */
    double p1_prob = 0.7 * p1_expected + 0.3 * wr_relative;

    /* Clamp to [0.15, 0.85] so no one is guaranteed to win/lose */
    if (p1_prob < 0.15) p1_prob = 0.15;
    if (p1_prob > 0.85) p1_prob = 0.85;

    /* Roll the dice */
    double roll = (double)rand() / (double)RAND_MAX;

    Player *winner, *loser;
    double expected_winner, expected_loser;

    if (roll < p1_prob) {
        winner = p1;
        loser  = p2;
        expected_winner = p1_expected;
        expected_loser  = 1.0 - p1_expected;
    } else {
        winner = p2;
        loser  = p1;
        expected_winner = 1.0 - p1_expected;
        expected_loser  = p1_expected;
    }

    /* Update win/loss records */
    winner->wins++;
    loser->losses++;

    /* Elo rating adjustment (K=32), ensuring at least 1 point change */
    int winner_change = (int)round(32.0 * (1.0 - expected_winner));
    int loser_change  = (int)round(32.0 * (0.0 - expected_loser));
    if (winner_change < 1) winner_change = 1;
    if (loser_change > -1) loser_change = -1;

    winner->rating += winner_change;
    loser->rating  += loser_change;

    /* Clamp ratings to [100, 5000] */
    if (winner->rating < 100) winner->rating = 100;
    if (winner->rating > 5000) winner->rating = 5000;
    if (loser->rating < 100) loser->rating = 100;
    if (loser->rating > 5000) loser->rating = 5000;

    /* Set players available */
    winner->status = availablePlayers;
    loser->status  = availablePlayers;
    winner->queuetime = 0;
    loser->queuetime  = 0;

    /* Release server load */
    for (size_t i = 0; i < sys->servers.count; i++) {
        if (sys->servers.servers[i].region == m->server_region &&
            sys->servers.servers[i].current_load > 0) {
            serverpool_remove_load(&sys->servers, (int)i);
            break;
        }
    }

    /* Update match record */
    m->status = MATCH_COMPLETED;
    m->completion_time = time(NULL);
    m->winner_id = winner->id;

    /* Store rating changes relative to player IDs */
    if (winner->id == m->player1_id) {
        m->p1_rating_change = winner_change;
        m->p2_rating_change = loser_change;
    } else {
        m->p1_rating_change = loser_change;
        m->p2_rating_change = winner_change;
    }

    sys->stats.total_matches_completed++;

    if (sys->verbose) {
        printf("Match #%d completed: %s (Rating %+d) beat %s (Rating %+d) | P1 win prob: %.0f%%\n",
               m->match_id, winner->name, winner_change,
               loser->name, loser_change, p1_prob * 100.0);
    }
}

void mms_complete_all_matches(MatchmakingSystem *sys) {
    int completed = 0;
    for (size_t i = 0; i < sys->matches.size; i++) {
        if (sys->matches.data[i].status == MATCH_ACTIVE) {
            mms_complete_match(sys, sys->matches.data[i].match_id);
            completed++;
        }
    }
    if (sys->verbose) {
        printf("\nCompleted %d match(es).\n", completed);
    }
}

/* ============================================================
 *  STATISTICS
 * ============================================================ */

void mms_print_weights(const MatchWeights *w) {
    printf("  Rating  : %.2f%%\n", w->w_rating  * 100.0);
    printf("  Ping    : %.2f%%\n", w->w_ping    * 100.0);
    printf("  Region  : %.2f%%\n", w->w_region  * 100.0);
    printf("  WinRate : %.2f%%\n", w->w_winrate * 100.0);
    if (w->w_wait > 0.0001) {
        printf("  Wait    : %.2f%%\n", w->w_wait    * 100.0);
    } else {
        printf("  Wait    : Decoupled (acts via candidate scarcity relaxation)\n");
    }
}

void mms_print_stats(const MatchmakingSystem *sys) {
    const MatchmakingStats *s = &sys->stats;

    printf("\n============= MATCHMAKING STATISTICS =============\n");
    printf("Total players in database : %zu\n", sys->players.size);

    /* Count by status */
    size_t avail = 0, waiting = 0, in_match = 0;
    for (size_t i = 0; i < sys->players.size; i++) {
        switch (sys->players.data[i].status) {
            case availablePlayers: avail++; break;
            case waitingPlayers:   waiting++; break;
            case inmatchPlayers:   in_match++; break;
            default: break;
        }
    }
    printf("Available players         : %zu\n", avail);
    printf("Waiting in queue          : %zu\n", waiting);
    printf("Currently in match        : %zu\n", in_match);
    printf("Players in PQ             : %zu\n", pq_size(&sys->waiting_queue));

    /* Active / completed matches */
    size_t active_m = 0, completed_m = 0;
    for (size_t i = 0; i < sys->matches.size; i++) {
        if (sys->matches.data[i].status == MATCH_ACTIVE) active_m++;
        else completed_m++;
    }
    printf("Active matches            : %zu\n", active_m);
    printf("Completed matches         : %zu\n", completed_m);
    printf("Total matches created     : %zu\n", s->total_matches_created);

    if (s->total_matches_created > 0) {
        double n = (double)s->total_matches_created;
        printf("Avg wait time (per pair)  : %.1f s\n", s->total_wait_time / (n * 2));
        if (s->max_wait_time > 0.0) {
            printf("Max wait time observed    : %.1f s\n", s->max_wait_time);
        }
        printf("Avg rating difference     : %.1f\n", s->total_rating_diff / n);
        printf("Avg ping difference       : %.1f\n", s->total_ping_diff / n);
        printf("Avg compatibility score   : %.4f\n", s->total_compat_score / n);
        printf("Region mismatches         : %zu\n", s->total_region_mismatches);
        printf("Candidate expansions     : %zu\n", s->candidate_expansions);
    }
    printf("Unmatched players (cum.)  : %zu\n", s->unmatched_players);

    /* Server utilization */
    printf("\n--- Server Utilization ---\n");
    for (size_t i = 0; i < sys->servers.count; i++) {
        const Server *sv = &sys->servers.servers[i];
        printf("  %s: %d/%d active | %d total served\n",
               sv->name, sv->current_load, sv->capacity, sv->total_served);
    }

    printf("\n--- Current Weights ---\n");
    mms_print_weights(&s->last_weights);
    printf("==================================================\n");
}

/* ============================================================
 *  VIEW PLAYERS IN QUEUE (Priority Order)
 * ============================================================ */

void mms_print_queue(const MatchmakingSystem *sys) {
    size_t qsize = pq_size(&sys->waiting_queue);
    printf("\n------------------------------------------------------------------------\n");
    printf("                     PLAYERS IN MATCHMAKING QUEUE\n");
    printf("------------------------------------------------------------------------\n");

    if (qsize == 0) {
        printf(" Queue is currently empty.\n");
        printf("------------------------------------------------------------------------\n");
        return;
    }

    printf(" Rank | ID    | Name            | Rating | Region | Ping | Wait Time\n");
    printf("------+-------+-----------------+--------+--------+------+----------\n");

    /* Create temporary copy of PriorityQueue to extract in true priority order */
    PriorityQueue temp_pq;
    pq_init(&temp_pq, qsize);
    for (size_t i = 0; i < qsize; i++) {
        pq_insert(&temp_pq, sys->waiting_queue.nodes[i].player_id, sys->waiting_queue.nodes[i].join_time);
    }

    double now = (double)time(NULL);
    size_t rank = 1;

    while (!pq_is_empty(&temp_pq)) {
        PQNode top = pq_extract_min(&temp_pq);
        HashNode *hn = hashmap_search((HashMap *)&sys->players.index, top.player_id);
        if (!hn) continue;
        const Player *p = player_dbget((PlayerDatabase *)&sys->players, hn->playerIndex);
        if (!p) continue;

        double wait_sec = now - (double)p->queuetime;
        if (wait_sec < 0) wait_sec = 0;
        int mins = (int)(wait_sec / 60.0);
        int secs = (int)fmod(wait_sec, 60.0);

        char rank_str[8];
        if (rank == 1) snprintf(rank_str, sizeof(rank_str), "#1");
        else snprintf(rank_str, sizeof(rank_str), "#%zu", rank);

        printf(" %-4s | %-5d | %-15s | %-6d | %-6s | %3dms| %02dm %02ds\n",
               rank_str, p->id, p->name, p->rating, regionToString(p->region),
               p->ping, mins, secs);
        rank++;
    }

    pq_destroy(&temp_pq);
    printf("------------------------------------------------------------------------\n");
    printf(" Total waiting: %zu player(s) | Order: Min-Heap (Longest wait first)\n", qsize);
    printf("------------------------------------------------------------------------\n");
}

/* ============================================================
 *  CURATED DEMO SHOWCASE DATASET
 * ============================================================ */

int mms_load_demo_showcase(MatchmakingSystem *sys) {
    /* Clear current runtime state */
    player_dbclear(&sys->players);
    matchhistory_destroy(&sys->matches);
    matchhistory_init(&sys->matches);
    pq_destroy(&sys->waiting_queue);
    pq_init(&sys->waiting_queue, 64);
    serverpool_init(&sys->servers);
    memset(&sys->stats, 0, sizeof(MatchmakingStats));
    sys->next_match_id = 1;

    /* 24 diverse, realistic demo players across 4 regions */
    struct {
        const char *name;
        int rating;
        int ping;
        Region region;
        int wins;
        int losses;
        int in_queue;       /* 1 = pre-enqueue for showcase */
        int wait_offset_sec;/* simulated wait time in seconds */
    } roster[] = {
        /* India Region */
        { "Aarav_IND",   2250, 18, REG_IND, 88, 22, 1, 180 }, /* Pro */
        { "Vikram_IND",  2180, 22, REG_IND, 74, 26, 1, 140 }, /* Pro */
        { "Priya_IND",   1550, 25, REG_IND, 42, 38, 1,  90 }, /* Mid */
        { "Rohan_IND",   1520, 20, REG_IND, 40, 40, 1,  60 }, /* Mid */
        { "Arjun_IND",   1100, 32, REG_IND, 12, 28, 0,   0 }, /* Novice */
        { "Diya_IND",     980, 35, REG_IND,  8, 32, 0,   0 }, /* Novice */

        /* Singapore Region */
        { "Wei_SIG",     2300, 28, REG_SIG, 92, 18, 1, 160 }, /* Pro */
        { "Li_SIG",      1600, 32, REG_SIG, 50, 35, 1,  75 }, /* Mid */
        { "Kavya_SIG",   1580, 30, REG_SIG, 48, 42, 0,   0 }, /* Mid */
        { "Kenji_SIG",   1490, 35, REG_SIG, 38, 42, 0,   0 }, /* Mid */
        { "Mei_SIG",     1150, 40, REG_SIG, 15, 25, 0,   0 }, /* Novice */
        { "Chen_SIG",    1020, 38, REG_SIG, 10, 30, 0,   0 }, /* Novice */

        /* Europe Region */
        { "Lars_EU",     2380, 22, REG_EU,  95, 15, 1, 110 }, /* Pro */
        { "Elena_EU",    1680, 25, REG_EU,  55, 30, 1,  45 }, /* Mid */
        { "Marcus_EU",   1620, 24, REG_EU,  50, 40, 0,   0 }, /* Mid */
        { "Sophie_EU",   1500, 26, REG_EU,  45, 45, 0,   0 }, /* Mid */
        { "Stefan_EU",   1200, 30, REG_EU,  20, 30, 0,   0 }, /* Novice */
        { "Chloe_EU",    1050, 28, REG_EU,  14, 36, 0,   0 }, /* Novice */

        /* North America Region */
        { "Alex_NA",     2290, 15, REG_NA,  85, 20, 0,   0 }, /* Pro */
        { "Sarah_NA",    1720, 18, REG_NA,  58, 32, 0,   0 }, /* Mid */
        { "Jordan_NA",   1560, 19, REG_NA,  46, 44, 0,   0 }, /* Mid */
        { "Tyler_NA",    1480, 22, REG_NA,  40, 45, 0,   0 }, /* Mid */
        { "Emily_NA",    1180, 25, REG_NA,  18, 32, 0,   0 }, /* Novice */
        { "Zach_NA",      950, 28, REG_NA,   9, 41, 0,   0 }  /* Novice */
    };

    size_t count = sizeof(roster) / sizeof(roster[0]);
    time_t now = time(NULL);
    int queued_count = 0;

    for (size_t i = 0; i < count; i++) {
        Player p;
        int pid = 1001 + (int)i;
        PlayerStatus st = roster[i].in_queue ? waitingPlayers : availablePlayers;
        time_t qt = roster[i].in_queue ? (now - (time_t)roster[i].wait_offset_sec) : 0;

        playerInit(&p, pid, roster[i].name, roster[i].rating, roster[i].ping,
                   roster[i].region, st, roster[i].wins, roster[i].losses, qt);
        player_dbadd(&sys->players, p);

        if (st == waitingPlayers) {
            pq_insert(&sys->waiting_queue, p.id, p.queuetime);
            queued_count++;
        }
    }

    /* Persist to disk files so demo data survives restarts */
    mms_save_players(sys, "data/players.dat");
    mms_save_matches(sys, "data/matches.dat");
    mms_save_servers(sys, "data/servers.dat");

    printf("\n------------------------------------------------------------\n");
    printf("         Showcase Demo Dataset Loaded Successfully\n");
    printf("------------------------------------------------------------\n");
    printf(" - Registered Players: %zu (across IND, SIG, EU, NA)\n", count);
    printf(" - Pre-Queued Players: %d in waiting queue\n", queued_count);
    printf(" - Saved to files    : data/*.dat\n");
    printf("------------------------------------------------------------\n");
    printf(" Next suggestions:\n");
    printf("   Option 19 -> View players in waiting queue\n");
    printf("   Option  7 -> Run matchmaking\n");
    printf("   Option  8 -> View created matches\n");
    printf("   Option  9 -> Complete matches and see Elo updates\n");
    printf("------------------------------------------------------------\n");

    return (int)count;
}


/* ============================================================
 *  PERSISTENCE – Binary .dat files
 *
 *  Format for each file:
 *    [size_t count] [struct × count]
 *
 *  NEVER persists pointers. HashMap and PQ are rebuilt on load.
 * ============================================================ */

static void ensure_data_dir(void) {
    _mkdir("data");  /* no-op if already exists */
}

int mms_save_players(const MatchmakingSystem *sys, const char *filepath) {
    ensure_data_dir();
    FILE *f = fopen(filepath, "wb");
    if (!f) { printf("Cannot open %s for writing.\n", filepath); return 0; }

    size_t count = sys->players.size;
    fwrite(&count, sizeof(size_t), 1, f);

    for (size_t i = 0; i < count; i++) {
        Player p = sys->players.data[i];
        fwrite(&p, sizeof(Player), 1, f);
    }

    fclose(f);
    printf("Saved %zu players to %s\n", count, filepath);
    return 1;
}

int mms_load_players(MatchmakingSystem *sys, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) { return 0; }  /* silently fail on first run */

    size_t count;
    if (fread(&count, sizeof(size_t), 1, f) != 1) { fclose(f); return 0; }

    for (size_t i = 0; i < count; i++) {
        Player p;
        if (fread(&p, sizeof(Player), 1, f) != 1) break;
        player_dbadd(&sys->players, p);
        /* Rebuild waiting queue and priority queue from loaded player state */
        if (p.status == waitingPlayers) {
            if (p.queuetime == 0) p.queuetime = time(NULL);
            pq_insert(&sys->waiting_queue, p.id, p.queuetime);
        }
    }

    fclose(f);
    printf("Loaded %zu players from %s\n", count, filepath);
    return 1;
}

int mms_save_matches(const MatchmakingSystem *sys, const char *filepath) {
    ensure_data_dir();
    FILE *f = fopen(filepath, "wb");
    if (!f) { printf("Cannot open %s for writing.\n", filepath); return 0; }

    size_t count = sys->matches.size;
    fwrite(&count, sizeof(size_t), 1, f);
    fwrite(sys->matches.data, sizeof(Match), count, f);

    fclose(f);
    printf("Saved %zu matches to %s\n", count, filepath);
    return 1;
}

int mms_load_matches(MatchmakingSystem *sys, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return 0;

    size_t count;
    if (fread(&count, sizeof(size_t), 1, f) != 1) { fclose(f); return 0; }

    for (size_t i = 0; i < count; i++) {
        Match m;
        if (fread(&m, sizeof(Match), 1, f) != 1) break;
        if (m.match_id >= sys->next_match_id) {
            sys->next_match_id = m.match_id + 1;
        }
        matchhistory_add(&sys->matches, m);
    }

    fclose(f);
    printf("Loaded %zu matches from %s\n", count, filepath);
    return 1;
}

int mms_save_servers(const MatchmakingSystem *sys, const char *filepath) {
    ensure_data_dir();
    FILE *f = fopen(filepath, "wb");
    if (!f) { printf("Cannot open %s for writing.\n", filepath); return 0; }

    fwrite(&sys->servers.count, sizeof(size_t), 1, f);
    fwrite(sys->servers.servers, sizeof(Server), sys->servers.count, f);

    fclose(f);
    printf("Saved %zu servers to %s\n", sys->servers.count, filepath);
    return 1;
}

int mms_load_servers(MatchmakingSystem *sys, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return 0;

    size_t count;
    if (fread(&count, sizeof(size_t), 1, f) != 1) { fclose(f); return 0; }
    if (count == 0) { fclose(f); return 0; }
    if (count > MAX_SERVERS) count = MAX_SERVERS;

    sys->servers.count = count;
    fread(sys->servers.servers, sizeof(Server), count, f);

    fclose(f);
    printf("Loaded %zu servers from %s\n", count, filepath);
    return 1;
}

/* ============================================================
 *  SIMULATION – Generate random players
 * ============================================================ */

void mms_generate_players(MatchmakingSystem *sys, int count) {
    int max_id = 100000;
    for (size_t i = 0; i < sys->players.size; i++) {
        if (sys->players.data[i].id >= max_id) {
            max_id = sys->players.data[i].id + 1;
        }
    }
    int sim_id = max_id;

    for (int i = 0; i < count; i++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "Player_%05d", sim_id + i);

        /* Rating: average of two randoms for a bell-curve-ish distribution */
        int r1 = 500 + rand() % 2001;  /* 500-2500 */
        int r2 = 500 + rand() % 2001;
        int rating = (r1 + r2) / 2;

        int ping = 10 + rand() % 241;      /* 10-250 */
        Region region = (Region)(rand() % NUM_REGIONS);
        int wins  = rand() % 201;
        int losses = rand() % 201;

        playerInit(&p, sim_id++, name, rating, ping, region,
                   availablePlayers, wins, losses, 0);
        player_dbadd(&sys->players, p);
    }
    printf("Generated %d simulated players.\n", count);
}

void mms_run_simulation(MatchmakingSystem *sys, int num_players) {
    printf("\n################ SIMULATION START ################\n");
    printf("Generating %d players...\n", num_players);

    mms_generate_players(sys, num_players);

    /* Add all generated players to matchmaking queue */
    /* They are the last num_players entries in the database */
    size_t start = sys->players.size - (size_t)num_players;
    time_t base_time = time(NULL);

    for (size_t i = start; i < sys->players.size; i++) {
        Player *p = player_dbget(&sys->players, i);
        p->status = waitingPlayers;
        /* Stagger queue times slightly so priority queue is meaningful */
        p->queuetime = base_time - (time_t)(sys->players.size - i);
        pq_insert(&sys->waiting_queue, p->id, p->queuetime);
    }

    printf("All %d players added to matchmaking queue.\n", num_players);

    /* Run matchmaking */
    mms_run_matchmaking(sys);

    /* Print stats after matchmaking */
    printf("\n--- After Matchmaking ---\n");
    mms_print_stats(sys);

    /* Complete all matches */
    printf("\n--- Completing All Matches ---\n");
    mms_complete_all_matches(sys);

    /* Print final stats */
    printf("\n--- Final Statistics ---\n");
    mms_print_stats(sys);

    printf("################ SIMULATION END ##################\n");
}

/* ============================================================
 *  EXPERIMENTAL COMPARISON: 3-Way Benchmark
 *  Static vs Legacy Dynamic vs Calibrated Dynamic
 * ============================================================ */

void mms_compare_3way(MatchmakingSystem *sys, int num_players) {
    (void)sys;  /* Comparison uses independent systems */
    printf("\n========================================================================================\n");
    printf("                3-WAY MATCHMAKING BENCHMARK (N = %d Players)\n", num_players);
    printf("========================================================================================\n");

    unsigned int seed = (unsigned int)time(NULL);

    /* --- Run 1: Static Weights --- */
    srand(seed);
    MatchmakingSystem sys_static;
    mms_init(&sys_static);
    sys_static.use_dynamic_weights = MMS_WEIGHT_STATIC;
    sys_static.verbose = 0;
    mms_generate_players(&sys_static, num_players);

    time_t base_time = time(NULL);
    for (size_t i = 0; i < sys_static.players.size; i++) {
        Player *p = player_dbget(&sys_static.players, i);
        p->status = waitingPlayers;
        p->queuetime = base_time - (time_t)(sys_static.players.size - i);
        pq_insert(&sys_static.waiting_queue, p->id, p->queuetime);
    }
    int static_matches = mms_run_matchmaking(&sys_static);
    mms_complete_all_matches(&sys_static);
    MatchmakingStats ss = sys_static.stats;

    /* --- Run 2: Legacy Dynamic Weights --- */
    srand(seed);
    MatchmakingSystem sys_legacy;
    mms_init(&sys_legacy);
    sys_legacy.use_dynamic_weights = MMS_WEIGHT_LEGACY_DYNAMIC;
    sys_legacy.verbose = 0;
    mms_generate_players(&sys_legacy, num_players);

    base_time = time(NULL);
    for (size_t i = 0; i < sys_legacy.players.size; i++) {
        Player *p = player_dbget(&sys_legacy.players, i);
        p->status = waitingPlayers;
        p->queuetime = base_time - (time_t)(sys_legacy.players.size - i);
        pq_insert(&sys_legacy.waiting_queue, p->id, p->queuetime);
    }
    int legacy_matches = mms_run_matchmaking(&sys_legacy);
    mms_complete_all_matches(&sys_legacy);
    MatchmakingStats ls = sys_legacy.stats;

    /* --- Run 3: Calibrated Dynamic Weights --- */
    srand(seed);
    MatchmakingSystem sys_calib;
    mms_init(&sys_calib);
    sys_calib.use_dynamic_weights = MMS_WEIGHT_CALIBRATED;
    sys_calib.verbose = 0;
    mms_generate_players(&sys_calib, num_players);

    base_time = time(NULL);
    for (size_t i = 0; i < sys_calib.players.size; i++) {
        Player *p = player_dbget(&sys_calib.players, i);
        p->status = waitingPlayers;
        p->queuetime = base_time - (time_t)(sys_calib.players.size - i);
        pq_insert(&sys_calib.waiting_queue, p->id, p->queuetime);
    }
    int calib_matches = mms_run_matchmaking(&sys_calib);
    mms_complete_all_matches(&sys_calib);
    MatchmakingStats cs = sys_calib.stats;

    /* --- Print Side-by-Side Table --- */
    printf("%-28s %16s %18s %20s\n", "Metric", "Static", "Legacy Dynamic", "Calibrated Dynamic");
    printf("----------------------------------------------------------------------------------------\n");
    printf("%-28s %16d %18d %20d\n", "Matches Created",
           static_matches, legacy_matches, calib_matches);

    double sn = (ss.total_matches_created > 0) ? (double)ss.total_matches_created : 1.0;
    double ln = (ls.total_matches_created > 0) ? (double)ls.total_matches_created : 1.0;
    double cn = (cs.total_matches_created > 0) ? (double)cs.total_matches_created : 1.0;

    printf("%-28s %16.1f %18.1f %20.1f\n", "Avg Wait Time (s)",
           ss.total_wait_time / (sn * 2.0),
           ls.total_wait_time / (ln * 2.0),
           cs.total_wait_time / (cn * 2.0));

    printf("%-28s %16.1f %18.1f %20.1f\n", "Max Wait Time (s)",
           ss.max_wait_time, ls.max_wait_time, cs.max_wait_time);

    printf("%-28s %16.1f %18.1f %20.1f\n", "Avg Rating Diff",
           ss.total_rating_diff / sn,
           ls.total_rating_diff / ln,
           cs.total_rating_diff / cn);

    printf("%-28s %16.1f %18.1f %20.1f\n", "Avg Ping Diff",
           ss.total_ping_diff / sn,
           ls.total_ping_diff / ln,
           cs.total_ping_diff / cn);

    printf("%-28s %16zu %18zu %20zu\n", "Region Mismatches",
           ss.total_region_mismatches, ls.total_region_mismatches, cs.total_region_mismatches);

    printf("%-28s %16zu %18zu %20zu\n", "Candidate Expansions",
           ss.candidate_expansions, ls.candidate_expansions, cs.candidate_expansions);

    printf("%-28s %16.4f %18.4f %20.4f\n", "Avg Match Score",
           ss.total_compat_score / sn,
           ls.total_compat_score / ln,
           cs.total_compat_score / cn);

    printf("%-28s %16zu %18zu %20zu\n", "Unmatched Players",
           ss.unmatched_players, ls.unmatched_players, cs.unmatched_players);

    printf("\n--- Static Weights Used ---\n");
    mms_print_weights(&ss.last_weights);
    printf("\n--- Legacy Dynamic Weights Used ---\n");
    mms_print_weights(&ls.last_weights);
    printf("\n--- Calibrated Dynamic Weights Used ---\n");
    mms_print_weights(&cs.last_weights);
    printf("========================================================================================\n");

    mms_destroy(&sys_static);
    mms_destroy(&sys_legacy);
    mms_destroy(&sys_calib);
}

void mms_compare_static_vs_dynamic(MatchmakingSystem *sys, int num_players) {
    mms_compare_3way(sys, num_players);
}

/* ============================================================
 *  CALIBRATION EXPERIMENTAL SCENARIOS (10 Seeds Averaged)
 * ============================================================ */

typedef struct {
    double matches;
    double avg_wait;
    double max_wait;
    double rating_diff;
    double ping_diff;
    double region_mismatches;
    double expansions;
    double compat_score;
    double unmatched;
} BenchStats;

/* --- Generator A: Healthy Queue --- */
static void generate_scenario_a(MatchmakingSystem *sys, int count) {
    time_t base_time = time(NULL);
    for (int i = 0; i < count; i++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "Healthy_%03d", i + 1);
        int rating = 1350 + rand() % 301;          /* 1350-1650 (mean ~1500) */
        int ping = 15 + rand() % 31;               /* 15-45ms (low latency) */
        Region reg = (Region)(i % NUM_REGIONS);    /* perfectly balanced regions */
        time_t qtime = base_time - (time_t)(rand() % 121);
        playerInit(&p, 10000 + i, name, rating, ping, reg, waitingPlayers, 20, 20, qtime);
        player_dbadd(&sys->players, p);
        pq_insert(&sys->waiting_queue, p.id, p.queuetime);
    }
}

/* --- Generator B: Rating Scarcity (Extreme Outliers) --- */
static void generate_scenario_b(MatchmakingSystem *sys, int count) {
    time_t base_time = time(NULL);
    int gm_count = count * 5 / 100;
    if (gm_count < 1) gm_count = 1;
    int novice_count = gm_count;
    int normal_count = count - gm_count - novice_count;

    int idx = 0;
    for (int i = 0; i < normal_count; i++, idx++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "Mid_%03d", idx + 1);
        int rating = 1400 + rand() % 201;          /* 1400-1600 */
        int ping = 20 + rand() % 26;               /* 20-45ms */
        time_t qtime = base_time - (time_t)(10 + rand() % 36);
        playerInit(&p, 20000 + idx, name, rating, ping, REG_IND, waitingPlayers, 25, 25, qtime);
        player_dbadd(&sys->players, p);
        pq_insert(&sys->waiting_queue, p.id, p.queuetime);
    }
    for (int i = 0; i < gm_count; i++, idx++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "GM_%03d", i + 1);
        int rating = 2450 + rand() % 351;          /* 2450-2800 */
        int ping = 20 + rand() % 26;
        time_t qtime = base_time - (time_t)(160 + rand() % 81); /* starving GM */
        playerInit(&p, 20000 + idx, name, rating, ping, REG_IND, waitingPlayers, 90, 10, qtime);
        player_dbadd(&sys->players, p);
        pq_insert(&sys->waiting_queue, p.id, p.queuetime);
    }
    for (int i = 0; i < novice_count; i++, idx++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "Novice_%03d", i + 1);
        int rating = 400 + rand() % 301;           /* 400-700 */
        int ping = 20 + rand() % 26;
        time_t qtime = base_time - (time_t)(160 + rand() % 81); /* starving Novice */
        playerInit(&p, 20000 + idx, name, rating, ping, REG_IND, waitingPlayers, 5, 45, qtime);
        player_dbadd(&sys->players, p);
        pq_insert(&sys->waiting_queue, p.id, p.queuetime);
    }
}

/* --- Generator C: Region Scarcity (Geographic Imbalance) --- */
static void generate_scenario_c(MatchmakingSystem *sys, int count) {
    time_t base_time = time(NULL);
    int na_count = count * 85 / 100;
    int eu_count = count * 10 / 100;
    int ind_count = count - na_count - eu_count;

    int idx = 0;
    for (int i = 0; i < na_count; i++, idx++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "NA_%03d", i + 1);
        int rating = 1300 + rand() % 401;
        int ping = 20 + rand() % 26;
        time_t qtime = base_time - (time_t)(10 + rand() % 46);
        playerInit(&p, 30000 + idx, name, rating, ping, REG_NA, waitingPlayers, 30, 30, qtime);
        player_dbadd(&sys->players, p);
        pq_insert(&sys->waiting_queue, p.id, p.queuetime);
    }
    for (int i = 0; i < eu_count; i++, idx++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "EU_%03d", i + 1);
        int rating = 1300 + rand() % 401;
        int ping = 25 + rand() % 26;
        time_t qtime = base_time - (time_t)(30 + rand() % 56);
        playerInit(&p, 30000 + idx, name, rating, ping, REG_EU, waitingPlayers, 30, 30, qtime);
        player_dbadd(&sys->players, p);
        pq_insert(&sys->waiting_queue, p.id, p.queuetime);
    }
    for (int i = 0; i < ind_count; i++, idx++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "IND_Starve_%03d", i + 1);
        int rating = 1300 + rand() % 401;
        int ping = 25 + rand() % 26;
        time_t qtime = base_time - (time_t)(140 + rand() % 81); /* starving isolated region */
        playerInit(&p, 30000 + idx, name, rating, ping, REG_IND, waitingPlayers, 30, 30, qtime);
        player_dbadd(&sys->players, p);
        pq_insert(&sys->waiting_queue, p.id, p.queuetime);
    }
}

/* --- Generator D: High Ping Variation --- */
static void generate_scenario_d(MatchmakingSystem *sys, int count) {
    time_t base_time = time(NULL);
    for (int i = 0; i < count; i++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "PingVar_%03d", i + 1);
        int rating = 1400 + rand() % 201;          /* tightly clustered skill 1400-1600 */
        int ping = 15 + rand() % 246;              /* wide ping spread: 15-260ms */
        time_t qtime = base_time - (time_t)(10 + rand() % 71);
        playerInit(&p, 40000 + i, name, rating, ping, REG_NA, waitingPlayers, 20, 20, qtime);
        player_dbadd(&sys->players, p);
        pq_insert(&sys->waiting_queue, p.id, p.queuetime);
    }
}

/* --- Generator E: Small Queue --- */
static void generate_scenario_e(MatchmakingSystem *sys, int count) {
    time_t base_time = time(NULL);
    for (int i = 0; i < count; i++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "SmallQ_%02d", i + 1);
        int rating = 1200 + rand() % 601;          /* 1200-1800 */
        int ping = 20 + rand() % 61;               /* 20-80ms */
        Region reg = (Region)(rand() % NUM_REGIONS);
        time_t qtime = base_time - (time_t)(15 + rand() % 166);
        playerInit(&p, 50000 + i, name, rating, ping, reg, waitingPlayers, 15, 15, qtime);
        player_dbadd(&sys->players, p);
        pq_insert(&sys->waiting_queue, p.id, p.queuetime);
    }
}

/* --- Generator F: Long-Wait Players (Starvation Pressure) --- */
static void generate_scenario_f(MatchmakingSystem *sys, int count) {
    time_t base_time = time(NULL);
    int starved_count = count * 10 / 100;
    if (starved_count < 2) starved_count = 2;
    int fresh_count = count - starved_count;

    int idx = 0;
    for (int i = 0; i < starved_count; i++, idx++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "Starved_%02d", i + 1);
        int rating = 1300 + rand() % 401;
        int ping = 25 + rand() % 31;
        Region reg = (Region)(i % NUM_REGIONS);
        time_t qtime = base_time - (time_t)(210 + rand() % 91); /* 210-300s */
        playerInit(&p, 60000 + idx, name, rating, ping, reg, waitingPlayers, 40, 40, qtime);
        player_dbadd(&sys->players, p);
        pq_insert(&sys->waiting_queue, p.id, p.queuetime);
    }
    for (int i = 0; i < fresh_count; i++, idx++) {
        Player p;
        char name[MAX_NM_LEN];
        snprintf(name, MAX_NM_LEN, "Fresh_%03d", i + 1);
        int rating = 1300 + rand() % 401;
        int ping = 25 + rand() % 31;
        Region reg = (Region)(i % NUM_REGIONS);
        time_t qtime = base_time - (time_t)(2 + rand() % 14);   /* 2-15s */
        playerInit(&p, 60000 + idx, name, rating, ping, reg, waitingPlayers, 20, 20, qtime);
        player_dbadd(&sys->players, p);
        pq_insert(&sys->waiting_queue, p.id, p.queuetime);
    }
}

static void run_scenario_benchmark(const char *scenario_name,
                                  const char *description,
                                  void (*gen_fn)(MatchmakingSystem*, int),
                                  int count) {
    const int num_seeds = 10;
    unsigned int seeds[10] = {101, 202, 303, 404, 505, 606, 707, 808, 909, 1010};

    BenchStats s_stat, s_leg, s_cal;
    memset(&s_stat, 0, sizeof(BenchStats));
    memset(&s_leg,  0, sizeof(BenchStats));
    memset(&s_cal,  0, sizeof(BenchStats));

    for (int i = 0; i < num_seeds; i++) {
        unsigned int sd = seeds[i];

        /* Static */
        srand(sd);
        MatchmakingSystem sys_s;
        mms_init(&sys_s);
        sys_s.use_dynamic_weights = MMS_WEIGHT_STATIC;
        sys_s.verbose = 0;
        gen_fn(&sys_s, count);
        mms_run_matchmaking(&sys_s);
        mms_complete_all_matches(&sys_s);

        double sn = (sys_s.stats.total_matches_created > 0) ? (double)sys_s.stats.total_matches_created : 1.0;
        s_stat.matches           += (double)sys_s.stats.total_matches_created;
        s_stat.avg_wait          += (sys_s.stats.total_wait_time / (sn * 2.0));
        s_stat.max_wait          += sys_s.stats.max_wait_time;
        s_stat.rating_diff       += (sys_s.stats.total_rating_diff / sn);
        s_stat.ping_diff         += (sys_s.stats.total_ping_diff / sn);
        s_stat.region_mismatches += (double)sys_s.stats.total_region_mismatches;
        s_stat.expansions        += (double)sys_s.stats.candidate_expansions;
        s_stat.compat_score      += (sys_s.stats.total_compat_score / sn);
        s_stat.unmatched         += (double)sys_s.stats.unmatched_players;
        mms_destroy(&sys_s);

        /* Legacy Dynamic */
        srand(sd);
        MatchmakingSystem sys_l;
        mms_init(&sys_l);
        sys_l.use_dynamic_weights = MMS_WEIGHT_LEGACY_DYNAMIC;
        sys_l.verbose = 0;
        gen_fn(&sys_l, count);
        mms_run_matchmaking(&sys_l);
        mms_complete_all_matches(&sys_l);

        double ln = (sys_l.stats.total_matches_created > 0) ? (double)sys_l.stats.total_matches_created : 1.0;
        s_leg.matches           += (double)sys_l.stats.total_matches_created;
        s_leg.avg_wait          += (sys_l.stats.total_wait_time / (ln * 2.0));
        s_leg.max_wait          += sys_l.stats.max_wait_time;
        s_leg.rating_diff       += (sys_l.stats.total_rating_diff / ln);
        s_leg.ping_diff         += (sys_l.stats.total_ping_diff / ln);
        s_leg.region_mismatches += (double)sys_l.stats.total_region_mismatches;
        s_leg.expansions        += (double)sys_l.stats.candidate_expansions;
        s_leg.compat_score      += (sys_l.stats.total_compat_score / ln);
        s_leg.unmatched         += (double)sys_l.stats.unmatched_players;
        mms_destroy(&sys_l);

        /* Calibrated Dynamic */
        srand(sd);
        MatchmakingSystem sys_c;
        mms_init(&sys_c);
        sys_c.use_dynamic_weights = MMS_WEIGHT_CALIBRATED;
        sys_c.verbose = 0;
        gen_fn(&sys_c, count);
        mms_run_matchmaking(&sys_c);
        mms_complete_all_matches(&sys_c);

        double cn = (sys_c.stats.total_matches_created > 0) ? (double)sys_c.stats.total_matches_created : 1.0;
        s_cal.matches           += (double)sys_c.stats.total_matches_created;
        s_cal.avg_wait          += (sys_c.stats.total_wait_time / (cn * 2.0));
        s_cal.max_wait          += sys_c.stats.max_wait_time;
        s_cal.rating_diff       += (sys_c.stats.total_rating_diff / cn);
        s_cal.ping_diff         += (sys_c.stats.total_ping_diff / cn);
        s_cal.region_mismatches += (double)sys_c.stats.total_region_mismatches;
        s_cal.expansions        += (double)sys_c.stats.candidate_expansions;
        s_cal.compat_score      += (sys_c.stats.total_compat_score / cn);
        s_cal.unmatched         += (double)sys_c.stats.unmatched_players;
        mms_destroy(&sys_c);
    }

    /* Compute means */
    double k = (double)num_seeds;
    s_stat.matches           /= k;
    s_stat.avg_wait          /= k;
    s_stat.max_wait          /= k;
    s_stat.rating_diff       /= k;
    s_stat.ping_diff         /= k;
    s_stat.region_mismatches /= k;
    s_stat.expansions        /= k;
    s_stat.compat_score      /= k;
    s_stat.unmatched         /= k;

    s_leg.matches           /= k;
    s_leg.avg_wait          /= k;
    s_leg.max_wait          /= k;
    s_leg.rating_diff       /= k;
    s_leg.ping_diff         /= k;
    s_leg.region_mismatches /= k;
    s_leg.expansions        /= k;
    s_leg.compat_score      /= k;
    s_leg.unmatched         /= k;

    s_cal.matches           /= k;
    s_cal.avg_wait          /= k;
    s_cal.max_wait          /= k;
    s_cal.rating_diff       /= k;
    s_cal.ping_diff         /= k;
    s_cal.region_mismatches /= k;
    s_cal.expansions        /= k;
    s_cal.compat_score      /= k;
    s_cal.unmatched         /= k;

    printf("\n----------------------------------------------------------------------------------------\n");
    printf(" SCENARIO: %s (N = %d, %d Runs Averaged)\n", scenario_name, count, num_seeds);
    printf(" Condition: %s\n", description);
    printf("----------------------------------------------------------------------------------------\n");
    printf("%-26s %14s %18s %22s\n", "Metric", "Static", "Legacy Dynamic", "Calibrated Dynamic");
    printf("----------------------------------------------------------------------------------------\n");
    printf("%-26s %14.1f %18.1f %22.1f\n", "Matches Created",
           s_stat.matches, s_leg.matches, s_cal.matches);
    printf("%-26s %14.1f %18.1f %22.1f\n", "Avg Wait Time (s)",
           s_stat.avg_wait, s_leg.avg_wait, s_cal.avg_wait);
    printf("%-26s %14.1f %18.1f %22.1f\n", "Max Wait Time (s)",
           s_stat.max_wait, s_leg.max_wait, s_cal.max_wait);
    printf("%-26s %14.1f %18.1f %22.1f\n", "Avg Rating Diff",
           s_stat.rating_diff, s_leg.rating_diff, s_cal.rating_diff);
    printf("%-26s %14.1f %18.1f %22.1f\n", "Avg Ping Diff (ms)",
           s_stat.ping_diff, s_leg.ping_diff, s_cal.ping_diff);
    printf("%-26s %14.1f %18.1f %22.1f\n", "Region Mismatches",
           s_stat.region_mismatches, s_leg.region_mismatches, s_cal.region_mismatches);
    printf("%-26s %14.1f %18.1f %22.1f\n", "Candidate Expansions",
           s_stat.expansions, s_leg.expansions, s_cal.expansions);
    printf("%-26s %14.4f %18.4f %22.4f\n", "Avg Match Score",
           s_stat.compat_score, s_leg.compat_score, s_cal.compat_score);
    printf("%-26s %14.1f %18.1f %22.1f\n", "Unmatched Players",
           s_stat.unmatched, s_leg.unmatched, s_cal.unmatched);
    printf("----------------------------------------------------------------------------------------\n");
}

void mms_run_calibration_experiments(void) {
    printf("\n========================================================================================\n");
    printf("            SYSTEMATIC DYNAMIC WEIGHT CALIBRATION BENCHMARK SUITE\n");
    printf("            Running 6 Empirical Scenarios (10 Independent Seeds Each)\n");
    printf("========================================================================================\n");

    /* Scenario A */
    run_scenario_benchmark("Scenario A: Healthy Queue",
                           "Balanced skill (1350-1650), low ping (15-45ms), uniform regions, 100 players",
                           generate_scenario_a, 100);

    /* Scenario B */
    run_scenario_benchmark("Scenario B: Rating Scarcity (Extreme Outliers)",
                           "90 mid-skill (1400-1600), 5 Grandmasters (2450+), 5 Novices (400-700), starving outliers",
                           generate_scenario_b, 100);

    /* Scenario C */
    run_scenario_benchmark("Scenario C: Region Scarcity (Geographic Imbalance)",
                           "85 NA players, 10 EU players, 5 IND players (isolated & starving), 100 players total",
                           generate_scenario_c, 100);

    /* Scenario D */
    run_scenario_benchmark("Scenario D: High Ping Variation",
                           "Uniform skill (1400-1600) in same region, latency widely distributed (15-260ms)",
                           generate_scenario_d, 100);

    /* Scenario E */
    run_scenario_benchmark("Scenario E: Small Queue",
                           "Only 14 players waiting; high sensitivity to greedy pool shrinkage",
                           generate_scenario_e, 14);

    /* Scenario F */
    run_scenario_benchmark("Scenario F: Long-Wait Players (Starvation Pressure)",
                           "10 players waiting 210-300s, 90 players fresh (2-15s wait), 100 players total",
                           generate_scenario_f, 100);

    printf("\n========================================================================================\n");
    printf("                       ALL 6 BENCHMARK SCENARIOS COMPLETED\n");
    printf("========================================================================================\n");
}
