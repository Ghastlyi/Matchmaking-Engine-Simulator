#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "common.h"
#include "player.h"
#include "hashmap.h"
#include "matchmaking.h"

extern const char *regionToString(Region region);

/* ---------- helper: read & validate an integer ---------- */
static int read_int(const char *prompt) {
    int val;
    printf("%s", prompt);
    while (scanf("%d", &val) != 1) {
        printf("Invalid input. %s", prompt);
        while (getchar() != '\n');  /* flush stdin */
    }
    return val;
}

/* ---------- 4: Update Player Fields (Inline CRUD) ---------- */
static void menu_update_player_fields(Player *p) {
    printf("\n--- Update Player: %s (ID %d) ---\n", p->name, p->id);
    printf(" 1. Update Name\n");
    printf(" 2. Update Ping\n");
    printf(" 3. Update Region\n");
    printf(" 0. Cancel\n");
    printf("------------------------------------\n");
    int choice = read_int("Enter choice: ");
    switch (choice) {
        case 1: {
            char name[MAX_NM_LEN];
            printf("Enter new name: ");
            scanf(" %49s", name);
            strncpy(p->name, name, MAX_NM_LEN - 1);
            p->name[MAX_NM_LEN - 1] = '\0';
            printf("Name updated.\n");
            break;
        }
        case 2: {
            int ping = read_int("Enter new ping (ms): ");
            if (ping < 1) ping = 1;
            p->ping = ping;
            printf("Ping updated.\n");
            break;
        }
        case 3: {
            printf("Select region:\n 1. India (IND)\n 2. Singapore (SIG)\n 3. Europe (EU)\n 4. North America (NA)\n");
            int r = read_int("Enter choice (1-4): ");
            if (r >= 1 && r <= 4) {
                p->region = (Region)(r - 1);
                printf("Region updated.\n");
            } else {
                printf("Invalid region.\n");
            }
            break;
        }
        case 0:
            break;
        default:
            printf("Invalid choice.\n");
    }
}

/* ---------- 20: Load Demo / Showcase Data Menu ---------- */
static void menu_load_demo_data(MatchmakingSystem *sys) {
    printf("\n------------------------------------------------------------\n");
    printf("                 Load Demo / Test Data\n");
    printf("------------------------------------------------------------\n");
    printf(" 1. Load Curated Showcase Roster (24 players, 8 queued)\n");
    printf(" 2. Load 'data/test_players.dat' (10 test players)\n");
    printf(" 3. Load from custom file path\n");
    printf(" 0. Cancel\n");
    printf("------------------------------------------------------------\n");
    int choice = read_int("Enter choice (0-3): ");

    switch (choice) {
        case 1:
            mms_load_demo_showcase(sys);
            break;

        case 2:
            if (mms_load_players(sys, "data/test_players.dat")) {
                printf("\nLoaded test players from 'data/test_players.dat'.\n");
                printf("Database now has %zu player(s).\n", sys->players.size);
            } else {
                printf("\n'data/test_players.dat' not found. Choose Option 1 to load the demo showcase.\n");
            }
            break;

        case 3: {
            char filepath[256];
            printf("Enter path to .dat file: ");
            scanf(" %255s", filepath);
            if (mms_load_players(sys, filepath)) {
                printf("\nLoaded players from '%s'.\n", filepath);
                printf("Database now has %zu player(s).\n", sys->players.size);
            } else {
                printf("\nFailed to load data from '%s'.\n", filepath);
            }
            break;
        }

        case 0:
        default:
            break;
    }
}

/* ---------- Main Menu Loop ---------- */
void showMenu(MatchmakingSystem *sys) {
    int choice;

    while (1) {
        /* Count player statuses for the live header */
        size_t avail = 0, waiting = 0, in_match = 0;
        for (size_t i = 0; i < sys->players.size; i++) {
            switch (sys->players.data[i].status) {
                case availablePlayers: avail++; break;
                case waitingPlayers:   waiting++; break;
                case inmatchPlayers:   in_match++; break;
                default: break;
            }
        }

        /* Count active matches */
        size_t active_m = 0;
        for (size_t i = 0; i < sys->matches.size; i++) {
            if (sys->matches.data[i].status == MATCH_ACTIVE) active_m++;
        }

        printf("\n------------------------------------------------------------\n");
        printf("             MatchMaking Engine Simulator\n");
        printf("------------------------------------------------------------\n");
        printf(" Players: %-4zu | Available: %-4zu | In Match: %-4zu\n",
               sys->players.size, avail, in_match);
        printf(" Waiting: %-4zu | Servers:   %-4zu | Active Matches: %-4zu\n",
               waiting, sys->servers.count, active_m);
        const char *mode_str = "Static";
        if (sys->use_dynamic_weights == MMS_WEIGHT_CALIBRATED) mode_str = "Calibrated Dynamic";
        else if (sys->use_dynamic_weights == MMS_WEIGHT_LEGACY_DYNAMIC) mode_str = "Legacy Dynamic";
        printf(" Weight Mode: %s\n", mode_str);
        printf("------------------------------------------------------------\n");
        printf("  1. Register Player\n");
        printf("  2. Remove Player\n");
        printf("  3. Search Player\n");
        printf("  4. View / Update Profile\n");
        printf("  5. Join Matchmaking\n");
        printf("  6. Leave Matchmaking\n");
        printf("  7. Run Matchmaking\n");
        printf("  8. View Active Matches\n");
        printf("  9. Complete Matches\n");
        printf(" 10. View Servers\n");
        printf(" 11. Find Best Server\n");
        printf(" 12. View Statistics\n");
        printf(" 13. Run Simulation\n");
        printf(" 14. Empty Database\n");
        printf(" 15. Print All Players\n");
        printf(" 16. Select Matchmaking Weight Mode\n");
        printf(" 17. Benchmarks (3-Way Comparison & 6 Scenarios)\n");
        printf(" 18. Save Data\n");
        printf(" 19. View Queue (Waiting Players)\n");
        printf(" 20. Load Demo / Test Data\n");
        printf("  0. Exit\n");
        printf("------------------------------------------------------------\n");

        choice = read_int("Enter choice: ");

        switch (choice) {
        case 0:
            /* Save persistent state before exit */
            mms_save_players(sys, "data/players.dat");
            mms_save_matches(sys, "data/matches.dat");
            mms_save_servers(sys, "data/servers.dat");
            printf("Data saved. Goodbye!\n");
            return;

        case 1: {
            Player p = registerPlayer();
            player_dbadd(&sys->players, p);
            printf("Player '%s' registered with ID %d.\n", p.name, p.id);
            break;
        }

        case 2: {
            int id = read_int("\nEnter player ID to remove: ");
            HashNode *node = hashmap_search(&sys->players.index, id);
            if (!node) {
                printf("Player with ID %d not found.\n", id);
                break;
            }
            Player *p = player_dbget(&sys->players, node->playerIndex);
            if (p->status == inmatchPlayers) {
                printf("Cannot remove '%s' (ID %d): player is currently in an active match.\n",
                       p->name, p->id);
                break;
            }
            if (p->status == waitingPlayers) {
                pq_remove_by_id(&sys->waiting_queue, id);
            }
            player_dbremove(&sys->players, id);
            break;
        }

        case 3: {
            int id = read_int("\nEnter player ID to search: ");
            HashNode *node = hashmap_search(&sys->players.index, id);
            if (node == NULL) {
                printf("Player with ID %d not found.\n", id);
            } else {
                Player *player = player_dbget(&sys->players, node->playerIndex);
                printf("\nPlayer found:\n");
                playerPrint(player);
            }
            break;
        }

        case 4: {
            int id = read_int("\nEnter player ID: ");
            HashNode *node = hashmap_search(&sys->players.index, id);
            if (node == NULL) {
                printf("Player not found.\n");
            } else {
                Player *player = player_dbget(&sys->players, node->playerIndex);
                profileprint(player);
                int upd = read_int("Update this player profile? (1 = Yes, 0 = No): ");
                if (upd == 1) menu_update_player_fields(player);
            }
            break;
        }

        case 5: {
            int id = read_int("\nEnter player ID to join queue: ");
            mms_join_queue(sys, id);
            break;
        }

        case 6: {
            int id = read_int("\nEnter player ID to leave queue: ");
            mms_leave_queue(sys, id);
            break;
        }

        case 7:
            mms_run_matchmaking(sys);
            break;

        case 8:
            matchhistory_print_active(&sys->matches);
            break;

        case 9: {
            printf("\n--- Complete Matches ---\n");
            printf(" 1. Complete specific match by ID\n");
            printf(" 2. Complete all active matches\n");
            printf(" 0. Cancel\n");
            int sub = read_int("Enter choice (0-2): ");
            if (sub == 1) {
                int mid = read_int("Enter match ID: ");
                mms_complete_match(sys, mid);
            } else if (sub == 2) {
                mms_complete_all_matches(sys);
            }
            break;
        }

        case 10:
            serverpool_print(&sys->servers);
            break;

        case 11: {
            printf("\nSelect target region:\n");
            printf(" 1. India\n 2. Singapore\n 3. Europe\n 4. North America\n");
            int r = read_int("Enter choice (1-4): ");
            if (r >= 1 && r <= 4) {
                int idx = serverpool_find_best(&sys->servers, (Region)(r - 1), 0);
                if (idx >= 0) {
                    printf("Best server: ");
                    server_print(&sys->servers.servers[idx]);
                } else {
                    printf("No available server in pool.\n");
                }
            }
            break;
        }

        case 12:
            mms_print_stats(sys);
            break;

        case 13: {
            printf("\n--- Run Simulation ---\n");
            printf(" 1. 100 Players\n");
            printf(" 2. 500 Players\n");
            printf(" 3. 1000 Players\n");
            printf(" 4. 5000 Players\n");
            printf(" 5. Custom\n");
            printf(" 0. Cancel\n");
            int sub = read_int("Enter choice (0-5): ");
            int count = 0;
            switch (sub) {
                case 1: count = 100; break;
                case 2: count = 500; break;
                case 3: count = 1000; break;
                case 4: count = 5000; break;
                case 5: count = read_int("Enter player count: "); break;
                case 0:
                default: continue;
            }
            if (count > 0) mms_run_simulation(sys, count);
            break;
        }

        case 14:
            /* Clear everything in memory and on disk */
            player_dbclear(&sys->players);
            matchhistory_destroy(&sys->matches);
            matchhistory_init(&sys->matches);
            pq_destroy(&sys->waiting_queue);
            pq_init(&sys->waiting_queue, 64);
            serverpool_init(&sys->servers);
            memset(&sys->stats, 0, sizeof(MatchmakingStats));
            sys->next_match_id = 1;
            /* Persist empty state to disk immediately */
            mms_save_players(sys, "data/players.dat");
            mms_save_matches(sys, "data/matches.dat");
            mms_save_servers(sys, "data/servers.dat");
            printf("All database records, queues, matches, and files cleared.\n");
            break;

        case 15:
            player_dbPrint(&sys->players);
            break;

        case 16: {
            printf("\nSelect Matchmaking Weight Mode:\n");
            printf(" 1. Calibrated Dynamic (Quality + Scarcity Relaxation)\n");
            printf(" 2. Static Weights (Fixed weights)\n");
            printf(" 3. Legacy Dynamic (Uncalibrated baseline for research)\n");
            int m = read_int("Enter choice (1-3): ");
            if (m == 1) {
                sys->use_dynamic_weights = MMS_WEIGHT_CALIBRATED;
                printf("Mode set to: CALIBRATED DYNAMIC\n");
            } else if (m == 2) {
                sys->use_dynamic_weights = MMS_WEIGHT_STATIC;
                printf("Mode set to: STATIC\n");
            } else if (m == 3) {
                sys->use_dynamic_weights = MMS_WEIGHT_LEGACY_DYNAMIC;
                printf("Mode set to: LEGACY DYNAMIC\n");
            } else {
                printf("Invalid choice.\n");
            }
            break;
        }

        case 17: {
            printf("\n--- Matchmaking Comparison Benchmarks ---\n");
            printf(" 1. 3-Way Comparison (Static vs Legacy vs Calibrated Dynamic)\n");
            printf(" 2. Full 6-Scenario Experimental Calibration Suite (10 Seeds Averaged)\n");
            printf(" 0. Cancel\n");
            int sub = read_int("Enter choice (0-2): ");
            if (sub == 1) {
                int count = read_int("Enter player count for comparison (e.g. 100 or 500): ");
                if (count > 0) mms_compare_3way(sys, count);
            } else if (sub == 2) {
                mms_run_calibration_experiments();
            }
            break;
        }

        case 18:
            mms_save_players(sys, "data/players.dat");
            mms_save_matches(sys, "data/matches.dat");
            mms_save_servers(sys, "data/servers.dat");
            printf("All data saved to disk.\n");
            break;

        case 19:
            mms_print_queue(sys);
            break;

        case 20:
            menu_load_demo_data(sys);
            break;

        default:
            printf("Invalid choice. Please try again.\n");
        }
    }
}