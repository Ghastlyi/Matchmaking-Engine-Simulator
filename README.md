# Matchmaking Engine Simulator

An experimental DSA-based matchmaking simulator written in C (C11) using GCC/MinGW on Windows.


Player features are addded hopefully i covered all by now else i will revise
=======
## Features

- **Player Database**: Dynamic array + HashMap (ID → index) for O(1) average lookup
- **HashMap**: Separate chaining with linked lists for collision handling
- **Priority Queue**: Min-heap for processing longest-waiting players first
- **Compatibility Graph**: Adjacency list graph with players as vertices, compatibility scores as edge weights
- **Dynamic Weights**: Population-dependent compatibility weights that adapt to pool statistics
- **Probabilistic Outcomes**: Elo-inspired win probability with fairness clamping [15%, 85%]
- **Binary Persistence**: Save/load players, matches, and servers to .dat files
- **Simulation**: Generate 100–5000 players and observe matchmaking behavior
- **Experimental Comparison**: Side-by-side static vs dynamic weight comparison

## Build

```bash
gcc -Iinclude -Wall -Wextra -std=c11 src/main.c src/menu.c src/player.c src/hashmap.c src/match.c src/server.c src/priorityqueue.c src/graph.c src/matchmaking.c -o app.exe -lm
```

Or using make:
```bash
make
```

## Run

```bash
app.exe
```

## Run Tests (65 Automated Tests)

```bash
gcc -Iinclude -Wall -Wextra -std=c11 src/player.c src/hashmap.c src/match.c src/server.c src/priorityqueue.c src/graph.c src/matchmaking.c src/game.c tests/test_matchmaking.c -o tests/test.exe -lm
tests\test.exe
```

## Menu Options

```
 [1] PLAYER MANAGEMENT              [2] MATCHMAKING & QUEUE
  1. Register New Player              5. Join Matchmaking Queue
  2. Remove Player (Swap-with-Last)   6. Leave Matchmaking Queue
  3. Search Player by ID (HashMap)   19. View Players in Queue (Priority Order)
  4. View / Update Player Profile     7. Run Matchmaking Engine
 15. View All Registered Players      8. View Active Matches
                                      9. Complete Active Matches (Elo)

 [3] SERVERS & ANALYTICS            [4] SIMULATION, PERSISTENCE & DEMO
 10. View Global Server Pool         13. Run Multi-Player Simulation
 11. Find Best Server for Region     17. Compare Static vs Dynamic Models
 12. View Detailed Engine Stats      16. Toggle Dynamic / Static Weights
                                     20. Load Showcase / Demo Data
                                     18. Save Current State to Disk
                                     14. Empty Entire Database
                                      0. Save Data & Exit
```

## Showcase / Demo Walkthrough

To demo the project:
1. Start the app: `.\app.exe`
2. Select **Option 20** (`Load Showcase / Demo Data`) and pick **Option 1** (`Load Curated Showcase Roster`).
   - This automatically populates 24 players with realistic skills across all 4 regions, and pre-queues 8 players with staggered wait times!
3. Select **Option 19** (`View Players in Queue`) to show the Priority Queue in action.
4. Select **Option 7** (`Run Matchmaking Engine`) to see Dynamic Weights adapt to the queue population and generate matches.
5. Select **Option 8** (`View Active Matches`) to show server assignments.
6. Select **Option 9** (`Complete Active Matches`) to show the fair probabilistic outcomes and Elo rating updates.
7. Select **Option 12** (`View Statistics`) to show matchmaking metrics and server utilization.

## Project Structure

```
MatchMaking Engine Simulator/
├── include/
│   ├── common.h            # Constants, enums (Region, PlayerStatus)
│   ├── player.h            # Player struct, PlayerDatabase
│   ├── hashmap.h           # HashMap with separate chaining
│   ├── match.h             # Match struct, MatchHistory
│   ├── server.h            # Server struct, ServerPool
│   ├── priorityqueue.h     # Min-heap priority queue
│   ├── graph.h             # Adjacency list compatibility graph
│   ├── matchmaking.h       # Core engine, weights, system
│   ├── matchmakingsystem.h # Backward-compat wrapper
│   ├── menu.h              # CLI menu
│   └── game.h              # Game logic (legacy)
├── src/
│   ├── main.c              # Entry point, load/save
│   ├── menu.c              # Full menu implementation
│   ├── player.c            # Player CRUD, database ops
│   ├── hashmap.c           # HashMap implementation
│   ├── match.c             # Match management
│   ├── server.c            # Server pool management
│   ├── priorityqueue.c     # Min-heap implementation
│   ├── graph.c             # Graph implementation
│   └── matchmaking.c       # Core engine (~1000 lines)
├── tests/
│   └── test_matchmaking.c  # 52 automated tests
├── data/                   # Binary .dat persistence files
├── case-study/
│   ├── Research.md         # Full design documentation
│   ├── Architecture.png
│   └── image.png
├── Makefile
├── README.md
└── app.exe
```

## Documentation

See [case-study/Research.md](case-study/Research.md) for:
- Matchmaking theory (Elo, TrueSkill, MMR, Glicko-2)
- System architecture diagram
- Compatibility score formula
- Dynamic weight algorithm and reasoning
- Data structure justifications
- Complexity analysis
- Design decisions for viva defense
- Binary file formats
- Limitations
