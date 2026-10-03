#ifndef MATCHMAKING_H
#define MATCHMAKING_H

#include "player.h"
#include "match.h"
#include "server.h"
#include "priorityqueue.h"
#include "graph.h"

/* Compatibility weight factors - sum should always equal 1.0 */
typedef struct {
    double w_rating;    /* weight for rating difference factor  */
    double w_ping;      /* weight for ping difference factor    */
    double w_region;    /* weight for region compatibility      */
    double w_winrate;   /* weight for win-rate difference       */
    double w_wait;      /* weight for waiting time bonus        */
} MatchWeights;

/* Statistics about the current waiting pool (used for dynamic weights) */
typedef struct {
    size_t total_waiting;
    double avg_rating;
    double rating_stddev;
    double avg_ping;
    double ping_stddev;
    double avg_winrate;
    double winrate_stddev;
    double avg_wait_time;
    int    region_counts[NUM_REGIONS];
} PoolStats;

/* Cumulative matchmaking statistics */
typedef struct {
    size_t total_matches_created;
    size_t total_matches_completed;
    double total_wait_time;
    double max_wait_time;
    double total_rating_diff;
    double total_ping_diff;
    double total_compat_score;
    size_t unmatched_players;
    size_t total_region_mismatches;
    size_t candidate_expansions;
    MatchWeights last_weights;
} MatchmakingStats;

/* Weight mode selector */
typedef enum {
    MMS_WEIGHT_STATIC = 0,
    MMS_WEIGHT_CALIBRATED = 1,
    MMS_WEIGHT_LEGACY_DYNAMIC = 2
} MMSWeightMode;

/* ============================================================
 *  CALIBRATION CONSTANTS
 * ============================================================ */
#define CALIB_BASE_RATING_WEIGHT     0.45
#define CALIB_BASE_PING_WEIGHT       0.25
#define CALIB_BASE_REGION_WEIGHT     0.20
#define CALIB_BASE_WINRATE_WEIGHT    0.10

#define CALIB_MIN_RATING_WEIGHT      0.30
#define CALIB_MAX_RATING_WEIGHT      0.65
#define CALIB_MIN_PING_WEIGHT        0.15
#define CALIB_MAX_PING_WEIGHT        0.40
#define CALIB_MIN_REGION_WEIGHT      0.10
#define CALIB_MAX_REGION_WEIGHT      0.35
#define CALIB_MIN_WINRATE_WEIGHT     0.05
#define CALIB_MAX_WINRATE_WEIGHT     0.20

/* Candidate Scarcity & Expansion Thresholds */
#define CALIB_STRICT_RATING_DIFF     200
#define CALIB_MOD_RATING_DIFF        400
#define CALIB_MAX_RATING_EXPANSION   800
#define CALIB_STRICT_PING_DIFF       40
#define CALIB_MOD_PING_DIFF          80
#define CALIB_MAX_PING_EXPANSION     150
#define CALIB_MAX_WAIT_TOLERANCE     180.0
#define CALIB_CROSS_REGION_RELAX_REQ 0.75

/* Player-specific candidate scarcity and relaxation context */
typedef struct {
    int    player_id;
    int    strict_candidates;   /* Close rating (<=200), low ping (<=40), same region */
    int    moderate_candidates; /* Acceptable rating (<=400), acceptable ping (<=80) */
    int    broad_candidates;    /* Any eligible candidate */
    double wait_time_sec;       /* Current wait time in seconds */
    double scarcity_index;      /* [0.0, 1.0]: 0 = plentiful candidates, 1 = severe drought */
    double relaxation_factor;   /* [0.0, 1.0]: controls search window expansion */
    int    rating_window;       /* Expanded rating tolerance window */
    int    ping_window;         /* Expanded ping tolerance window */
    int    allow_cross_region;  /* Whether cross-region matching is permitted */
} PlayerScarcityContext;

/* The central matchmaking system */
typedef struct {
    PlayerDatabase   players;
    MatchHistory     matches;
    ServerPool       servers;
    PriorityQueue    waiting_queue;
    MatchmakingStats stats;
    int              use_dynamic_weights;   /* 0 = static, 1 = calibrated dynamic, 2 = legacy dynamic */
    int              next_match_id;         /* monotonically increasing match ID */
    int              verbose;               /* 1 = print individual matches, 0 = quiet */
} MatchmakingSystem;

/* ----- System Lifecycle ----- */
void mms_init(MatchmakingSystem *sys);
void mms_destroy(MatchmakingSystem *sys);

/* ----- Queue Operations ----- */
int  mms_join_queue(MatchmakingSystem *sys, int player_id);
int  mms_leave_queue(MatchmakingSystem *sys, int player_id);

/* ----- Matchmaking ----- */
int  mms_run_matchmaking(MatchmakingSystem *sys);

/* ----- Match Completion (probabilistic outcomes) ----- */
void mms_complete_match(MatchmakingSystem *sys, int match_id);
void mms_complete_all_matches(MatchmakingSystem *sys);

/* ----- Compatibility & Quality ----- */
double calculate_compatibility(const Player *a, const Player *b,
                               const MatchWeights *weights,
                               double current_time);
double calculate_quality_score(const Player *a, const Player *b,
                               const MatchWeights *weights);
int    check_eligibility(const Player *a, const Player *b, double current_time);
int    check_calibrated_eligibility(const Player *a, const Player *b,
                                   const PlayerScarcityContext *ctx_a,
                                   const PlayerScarcityContext *ctx_b);

/* ----- Scarcity & Relaxation ----- */
PlayerScarcityContext mms_evaluate_player_scarcity(const MatchmakingSystem *sys,
                                                  int player_id,
                                                  double current_time);

/* ----- Weight Computation ----- */
MatchWeights compute_static_weights(void);
MatchWeights compute_dynamic_weights(PlayerDatabase *db, PriorityQueue *pq);
MatchWeights compute_calibrated_weights(PlayerDatabase *db, PriorityQueue *pq);
PoolStats    compute_pool_stats(PlayerDatabase *db, PriorityQueue *pq);

/* ----- Statistics & Queue Inspection ----- */
void mms_print_stats(const MatchmakingSystem *sys);
void mms_print_weights(const MatchWeights *w);
void mms_print_queue(const MatchmakingSystem *sys);

/* ----- Demo & Showcase Data ----- */
int  mms_load_demo_showcase(MatchmakingSystem *sys);

/* ----- Persistence (binary .dat files) ----- */
int  mms_save_players(const MatchmakingSystem *sys, const char *filepath);
int  mms_load_players(MatchmakingSystem *sys, const char *filepath);
int  mms_save_matches(const MatchmakingSystem *sys, const char *filepath);
int  mms_load_matches(MatchmakingSystem *sys, const char *filepath);
int  mms_save_servers(const MatchmakingSystem *sys, const char *filepath);
int  mms_load_servers(MatchmakingSystem *sys, const char *filepath);

/* ----- Simulation ----- */
void mms_generate_players(MatchmakingSystem *sys, int count);
void mms_run_simulation(MatchmakingSystem *sys, int num_players);

/* ----- Experimental Comparison & Calibration Suite ----- */
void mms_compare_static_vs_dynamic(MatchmakingSystem *sys, int num_players);
void mms_compare_3way(MatchmakingSystem *sys, int num_players);
void mms_run_calibration_experiments(void);

#endif
