# Matchmaking Engine Simulator — Research & Design Documentation

> **Project Type**: Experimental DSA-based Matchmaking Simulator  
> **Language**: C11 (GCC/MinGW on Windows)  
> **Author**: Parth Pawar
> **Status**: Working simulation with dynamic compatibility weighting

---

## Table of Contents

1. [Background: Existing Matchmaking Systems](#1-background-existing-matchmaking-systems)
2. [My Approach](#2-my-approach)
3. [Current System Architecture](#3-current-system-architecture)
4. [Player Structure & ID Generation](#4-player-structure--id-generation)
5. [Compatibility Score](#5-compatibility-score)
6. [Dynamic Compatibility Weighting](#6-dynamic-compatibility-weighting)
7. [Candidate Filtering & Eligibility](#7-candidate-filtering--eligibility)
8. [Priority Queue](#8-priority-queue)
9. [Compatibility Graph](#9-compatibility-graph)
10. [Matchmaking Algorithm](#10-matchmaking-algorithm)
11. [Probabilistic Match Outcomes](#11-probabilistic-match-outcomes)
12. [Binary Persistence](#12-binary-persistence)
13. [Server System](#13-server-system)
14. [Simulation & Experimental Design](#14-simulation--experimental-design)
15. [Complexity Analysis](#15-complexity-analysis)
16. [Design Reasoning](#16-design-reasoning)
17. [Limitations](#17-limitations)
18. [Binary File Format](#18-binary-file-format)
19. [Dynamic Weight Calibration](#19-dynamic-weight-calibration)

---

## 1. Background: Existing Matchmaking Systems

### Elo Rating
Used in old games and games like chess where rating-based matchmaking is done.
Rating score is passed from loser player to the winner player.

Games like: Chess / Halo (Older versions)

Simplified Working:
1. Each player starts with a rating.
2. The system predicts who is more likely to win based on ratings.
3. After the match, ratings are adjusted:
   - Beat a stronger player → large rating increase
   - Beat a weaker player → small rating increase
   - Lose to a weaker player → large rating decrease
4. Players with similar ratings are matched together.

Basic Idea:

```
Rating -> Predict Result -> Play Match -> Update Rating
```

### TrueSkill / TrueSkill 2
Uses a Bayesian approach that tracks a player's average skill (μ) and the system's uncertainty (σ) to predict team performance.

Games like: Rocket League / Halo

Simplified Working:
1. Every player has an estimated skill level (μ).
2. The system also stores how confident it is about that estimate (σ).
3. After every match, both values are updated.
4. If the system is uncertain about a player's skill, their rating can change more significantly.
5. Teams are formed by predicting how likely different combinations are to win.

Formula (Bayesian statistical model):

```
Updated Belief = Old Belief(Prior) * New Data(Likelihood) / Overall Probability
```

Basic Idea:

```
Estimated Skill + Uncertainty -> Predict Team Strength -> Update After Match
```

### Matchmaking Rating (MMR)
A hidden internal rating that rises with wins and drops with losses, separating a player's cosmetic rank from their actual skill.

Games like: League of Legends

Simplified Working:
1. Every player has a hidden MMR.
2. Winning generally increases MMR.
3. Losing generally decreases MMR.
4. The matchmaking system primarily uses MMR to find suitable opponents.
5. Your visible rank may be different from your actual MMR.

Basic Idea:

```
Hidden MMR -> Find Similar MMR -> Match Players -> Update MMR
```

### Glicko-2 Variant
An evolution of Elo that introduces a "rating volatility" factor, which accounts for erratic performance or long breaks.

Games like: Counter Strike 2

Simplified Working:
1. Every player has a rating.
2. The system also estimates how certain that rating is.
3. It tracks rating volatility, which represents how consistently a player's performance changes.
4. A player whose performance suddenly changes can have their rating adjusted more aggressively.
5. Ratings become more stable when performance is consistent.

Basic Idea:

```
Rating + Uncertainty + Volatility -> Adjust Rating After Match
```

### Engagement-Optimized (EACM)
A controversial, behavioral model designed to maximize player retention by adjusting difficulty based on play patterns.

Games like: Apex Legends (EA Games)

Simplified Working:
1. The system collects behavioral and gameplay signals.
2. It estimates how a player is likely to respond to different opponents or match conditions.
3. Match selection can consider factors beyond pure skill.
4. The objective is to influence the overall player experience and engagement.

Basic Ideas:

```
Player Behavior + Skill + Match Conditions -> Select Match
```

---

## 2. My Approach

### Player Structure

```c
typedef struct{
    int id;
    char name[MAX_NM_LEN];
    int rating;
    int ping;
    Region region;
    PlayerStatus status;
    int wins;
    int losses;
    time_t queuetime;
} Player;
```

What each field determines for matchmaking:

| Field | Role in Matchmaking |
|-------|-------------------|
| `id` | Unique identifier. Based on `time(NULL) + offset` for simplicity |
| `rating` | Primary skill indicator. Similar ratings should play together |
| `ping` | Latency simulation. Only compatible pings should be matched |
| `region` | Server segregation. Prefer same-region matches |
| `wins/losses` | Win rate = wins/(wins+losses). Captures consistency beyond rating |
| `queuetime` | Queue join timestamp. Determines priority and relaxation urgency |
| `status` | available / waiting / in_match / inactive. Controls eligibility |

### Core Concept: Compatibility Score

The main concept is a **multi-factor compatibility score** calculated using normalized variables and **dynamically adjusted weights**.

Two approaches:
1. **Static weights** — hardcoded, one-size-fits-all
2. **Dynamic weights** — adjusted based on the current matchmaking population

### Data Structure Implications

- **Priority Queue (Min-Heap)**: Determines which waiting player gets processed first
- **Compatibility Graph**: Player = Vertex, Compatibility Score = Edge Weight

### Scalability Consideration

> What if 10,000 players are in the matchmaking list?

Solution: We do NOT compare every player against every other blindly.
- Partition by regions
- Filter by rating range (expanding over time)
- Only create edges between eligible candidates
- Process highest-priority (longest-waiting) players first

![Representation of usage of graphs](image.png)

---

## 3. Current System Architecture

```
                         MATCHMAKING SYSTEM
                                |
          +---------------------+---------------------+
          |                     |                     |
          v                     v                     v
   PlayerDatabase          Match History         Server Pool
          |
          +----------------------+
          |                      |
          v                      v
    Dynamic Array            HashMap
    Player records       ID -> array index
          |
          v
   Waiting Players
          |
          +----------------------+
          |                      |
          v                      v
    Priority Queue       Candidate Filtering
    (who first?)         (who is eligible?)
                                 |
                                 v
                       Compatibility Graph
                       (valid candidates)
                                 |
                                 v
                       Compatibility Score
                       (how well do they match?)
                                 |
                                 v
                            Match Created
                                 |
                                 v
                           Server Selected
                           
Binary .dat files sit underneath as persistence layer.
They are NOT the runtime data structures.
```

### Module Map

| Module | Header | Source | Purpose |
|--------|--------|--------|---------|
| Player & Database | `player.h` | `player.c` | Player struct, dynamic array, CRUD |
| HashMap | `hashmap.h` | `hashmap.c` | ID → index lookup, separate chaining |
| Match | `match.h` | `match.c` | Match records, history |
| Server | `server.h` | `server.c` | Server pool, region-based selection |
| Priority Queue | `priorityqueue.h` | `priorityqueue.c` | Min-heap for queue processing |
| Graph | `graph.h` | `graph.c` | Adjacency list compatibility graph |
| Matchmaking | `matchmaking.h` | `matchmaking.c` | Core engine, weights, scoring, persistence |
| Menu | `menu.h` | `menu.c` | CLI interface |

---

## 4. Player Structure & ID Generation

### ID Generation Strategy

**Original approach**: `int id = (int)time(NULL);`

**Problem identified**: Two registrations within the same second produce identical IDs.

**Solution implemented**: Static offset counter:
```c
static int id_offset = 0;
int id = (int)time(NULL) + id_offset++;
```

**Why this works**: The counter ensures monotonically increasing IDs even within the same second. For a simulator where players are registered sequentially (not concurrently), this is sufficient.

**Why we didn't use UUIDs**: UUIDs require external libraries or complex generation, adding unnecessary complexity for a student DSA project. The HashMap already handles uniqueness checking via `hashmap_search` before insertion.

### String Safety

`playerInit()` uses `strncpy` with explicit null-termination instead of `strcpy` to prevent buffer overflow:
```c
strncpy(player->name, name, MAX_NM_LEN - 1);
player->name[MAX_NM_LEN - 1] = '\0';
```

---

## 5. Compatibility Score

### Problem Statement

We cannot simply add raw values:
```
score = rating + ping + wins   // WRONG
```

These values have completely different scales:
- Rating: 100–5000
- Ping: 10–300ms
- Win rate: 0.0–1.0

### Normalization

Each factor is normalized to approximately [0, 1]:

| Factor | Formula | Range |
|--------|---------|-------|
| Rating diff | `abs(A.rating - B.rating) / 3000` | [0, ~1.67] |
| Ping diff | `abs(A.ping - B.ping) / 300` | [0, ~1] |
| Region | 0 if same, 1 if different | {0, 1} |
| Win-rate diff | `abs(wr_A - wr_B)` | [0, 1] |
| Wait bonus | `avg_wait / 180` (capped at 1.0) | [0, 1] |

### Score Formula

```
score = W_rating  × rating_factor
      + W_ping    × ping_factor
      + W_region  × region_factor
      + W_winrate × winrate_factor
      - W_wait    × wait_bonus
```

**LOWER score = BETTER match.**

The wait bonus is **subtracted** because longer waiting should make players appear *more* compatible (relaxing match quality requirements).

The score is floored at 0.001 to avoid zero/negative scores.

### Why Subtraction for Wait?

If two players have been waiting 3+ minutes, the system should be more willing to match them even if they aren't perfect. Subtracting the wait bonus effectively "pulls down" the score, making the match look better.

---

## 6. Dynamic Compatibility Weighting

### Why Static Weights Are Insufficient

Static weights like:
```
Rating: 35%, Ping: 20%, Region: 20%, WinRate: 10%, Wait: 15%
```

assume the matchmaking population is always the same. But:

1. **Population varies**: At 3 AM there may be 10 players; at peak time there may be 5000.
2. **Distribution varies**: Sometimes all players have similar ratings but wildly different pings.
3. **Region imbalance**: Some regions may have very few players.
4. **Queue pressure**: When people wait too long, match quality should be sacrificed.

### What Changes in the Population

| Signal | Effect |
|--------|--------|
| High rating variance | Harder to find same-rating opponents |
| Low ping variance | Easy to match on ping → can weight it more |
| Few players in a region | Region restriction causes long waits |
| Long average wait time | System should prioritize matching speed |
| Small total pool | All restrictions should relax |

### Dynamic Weight Algorithm

#### Step 1: Compute Pool Statistics

For all waiting players, calculate:
- Mean and standard deviation of: rating, ping, win-rate
- Player count per region
- Average waiting time

#### Step 2: Compute Scarcity/Spread Signals

```
rating_spread    = rating_stddev / avg_rating     (coefficient of variation)
ping_spread      = ping_stddev / avg_ping
winrate_spread   = winrate_stddev / avg_winrate
region_imbalance = 1 - (min_region / max_region)  (among non-zero regions)
wait_pressure    = avg_wait_time / 120s           (capped at 1.0)
```

**Coefficient of variation** (stddev/mean) is used because it gives a scale-independent measure of spread. A rating stddev of 200 means different things if the mean is 800 vs 2500.

#### Step 3: Adjust Base Weights

```
raw_rating  = base_rating  × (1.0 - 0.5 × rating_spread)
raw_ping    = base_ping    × (1.0 - 0.5 × ping_spread)
raw_region  = base_region  × (1.0 - 0.5 × region_imbalance)
raw_winrate = base_winrate × (1.0 - 0.5 × winrate_spread)
raw_wait    = base_wait    × (1.0 + 2.0 × wait_pressure)
```

**Intuition**:
- **High spread → reduce weight**: If rating spread is high, players are very diverse in rating. Being strict about rating matching becomes impractical, so we reduce its importance.
- **Low spread → weight stays high**: If everyone has similar ping, we can afford to weight ping matching strongly.
- **Wait pressure → increase wait weight**: The longer people wait on average, the more we prioritize getting them matched.

#### Step 4: Clamp Weights

Each weight is clamped to [0.05, 0.60]:
- **Minimum 5%**: No factor should be completely ignored
- **Maximum 60%**: No single factor should dominate

#### Step 5: Small-Pool Boost

If fewer than 6 players are waiting, add 0.1 to the wait weight. This ensures small pools don't cause infinite waiting.

#### Step 6: Normalize

Divide each weight by the sum so they total exactly 1.0:
```
W_i = raw_i / sum(all raw weights)
```

### Design Reasoning for Dynamic Weights

**Initial idea**: Hardcoded weights.

**Problem**: The same weights don't work equally well for 10 players vs 1000 players, or for a pool where everyone is rated 1500 vs a pool spanning 500–3000.

**Alternative considered**: Completely random or ML-based adaptive weights.

**Problem**: Unstable, unjustifiable in a viva, requires training data we don't have.

**Alternative considered**: Per-player individual weight adjustment.

**Problem**: Too complex, harder to reason about, may cause asymmetric matchmaking.

**Final approach**: **Population-dependent weighting based on measurable statistical signals**.

**Why this works**: 
- It's deterministic and explainable
- It responds to actual pool conditions
- It preserves fairness (same formula for everyone)
- It naturally handles edge cases (small pools, long waits)
- The coefficient of variation is a standard statistical measure

**Trade-offs**:
- Computing pool stats adds O(N) overhead per matchmaking cycle
- Weight changes are global, not per-player (simpler but less fine-grained)
- The 0.5 and 2.0 multipliers are heuristic constants, not optimally tuned

---

## 7. Candidate Filtering & Eligibility

Before computing the full compatibility score (which involves floating-point math), we apply fast integer-based eligibility checks:

### Rating Window

```
max_allowed_rating_diff = 500 + (max_wait_seconds × 5)
```

- Base window: 500 rating points
- Expands by 5 per second of waiting
- After 100 seconds: window = 1000
- After 200 seconds: window = 1500

This ensures players with extreme ratings eventually find matches.

### Region Restriction

- Same region: always eligible
- Different region: eligible only if either player has waited > 60 seconds

This prevents cross-region matches when there are local candidates, but relaxes when waiting becomes excessive.

### Why Pre-filtering?

For N waiting players, there are N(N-1)/2 potential pairs. Eligibility filtering eliminates most pairs before the expensive compatibility calculation, reducing practical graph size.

---

## 8. Priority Queue

### Implementation: Min-Heap

```
                    [earliest joiner]
                   /                 \
          [2nd earliest]        [3rd earliest]
         /            \
   [4th earliest]  [5th earliest]
```

- **Structure**: Array-based binary min-heap
- **Key**: `join_time` (earlier time = higher priority)
- **Operations**:
  - Insert: O(log N) — sift up
  - Extract min: O(log N) — sift down
  - Remove by ID: O(N) — linear scan + sift
  - Peek: O(1)

### What the Priority Queue Determines

The priority queue answers: **"WHO GETS CONSIDERED FIRST?"**

It does **NOT** answer: **"WHO SHOULD BE MATCHED WITH WHOM?"**

The player who has waited longest gets processed first. For that player, the compatibility graph determines the best opponent.

### Why Not Just a Regular Queue?

A regular FIFO queue would process in strict arrival order. But if a player joins, leaves, and rejoins, their position in a FIFO queue would reset. The min-heap correctly handles this because it's keyed on the actual queue join time.

Additionally, the heap allows efficient removal when a player leaves the queue (option 6 in the menu).

---

## 9. Compatibility Graph

### Structure: Adjacency List

```
Player A ──(0.12)──> Player B
    |                    |
    └──(0.38)──> Player C ──(0.17)──> Player B
```

Each vertex represents a waiting player. Each edge represents a valid match candidate with its compatibility score as weight.

### Why a Graph?

Compatibility is inherently a **relationship between pairs**. This naturally maps to a graph:
- Vertex = player
- Edge = valid candidate pair
- Weight = compatibility score

The graph allows us to:
1. Find the best match for any player in O(degree) time
2. Remove matched players and their edges
3. Visualize the matchmaking state

### Why Not a Full N×N Matrix?

For 1000 players, an adjacency matrix would use 1,000,000 entries. The adjacency list only stores actual eligible pairs, which is typically much less after filtering.

### Graph Lifecycle

The graph is **rebuilt every matchmaking cycle**:
1. Clear previous graph
2. Add waiting players as vertices
3. For each eligible pair, add edge with compatibility score
4. Greedy matching: extract from PQ, find best edge, match, remove both

The graph is a **runtime structure** — it is never persisted to disk.

---

## 10. Matchmaking Algorithm

### Complete Flow

```
1. Check queue size ≥ 2
2. Compute weights (static or dynamic)
3. Collect all waiting player IDs
4. Build compatibility graph:
   a. Add all waiting players as vertices
   b. For each pair (i, j):
      - Check eligibility
      - If eligible: compute compatibility score
      - Add edge with score as weight
5. Create temporary priority queue (sorted copy)
6. Greedy matching loop:
   a. Extract longest-waiting player from PQ
   b. Find best match (lowest edge weight) in graph
   c. If found and not already matched:
      - Create Match record
      - Select best server
      - Set both players to inmatchPlayers
      - Remove both from PQ and graph
      - Update statistics
   d. If not found: increment unmatched counter
7. Repeat until PQ empty
8. Print summary
```

### Why Greedy?

An optimal matching (e.g., minimum-weight perfect matching) would minimize total compatibility score across all pairs. However:
- It requires O(N³) algorithms (Hungarian/Blossom)
- It doesn't consider priority (who waited longest)
- It's overkill for a simulator

The greedy approach processes the longest-waiting player first and gives them the best available match. This naturally balances quality and fairness.

---

## 11. Probabilistic Match Outcomes

### Why Not Deterministic?

If the higher-rated player always wins, matches become predictable and the system can't simulate upsets, which are common in real games.

### Win Probability Formula

```
rating_advantage = (p1.rating - p2.rating) / 400.0
p1_expected = 1 / (1 + 10^(-rating_advantage))              // Elo formula

wr1 = p1.wins / (p1.wins + p1.losses)                       // Historical win rate
wr2 = p2.wins / (p2.wins + p2.losses)
wr_relative = (wr1 + wr2 > 0) ? wr1 / (wr1 + wr2) : 0.5     // Symmetric relative factor

p1_prob = 0.7 × p1_expected + 0.3 × wr_relative             // Combined
p1_prob = clamp(p1_prob, 0.15, 0.85)                         // Fairness clamp
```

**Why relative win rate?**
Using `wr1` alone would introduce artificial asymmetry: two players with identical 80% win rates would give `p1` a 59% win chance! Using `wr_relative = wr1 / (wr1 + wr2)` ensures that identical win rates yield exactly 0.50 (50%), preserving mathematical symmetry and genuine fairness.

The probability is **clamped to [15%, 85%]** so:
- No player has a guaranteed win (at least 15% chance for the underdog)
- No player has a guaranteed loss (at most 85% for the favorite)
- This reflects real-world variance in game outcomes

### Rating Adjustment (K=32)

After determining the winner:
```
winner_change = max(1, (int)round(K × (1 - expected_winner)))
loser_change  = min(-1, (int)round(K × (0 - expected_loser)))
```

- Every win awards at least +1 point (no zero-point victories)
- Every loss deducts at least -1 point
- Upset victories give larger rating gains
- Expected victories give smaller gains
- Ratings are clamped to [100, 5000]

### Why 70/30 Split?

70% weight on Elo expected score ensures rating is the primary predictor. 30% weight on historical win rate captures factors that rating alone doesn't (consistency, play style, etc.).

---

## 12. Binary Persistence

### Overview

```
RUNTIME                          DISK
───────                          ────
PlayerDatabase.data  ──save──>   data/players.dat
MatchHistory.data    ──save──>   data/matches.dat
ServerPool.servers   ──save──>   data/servers.dat

PlayerDatabase       <──load──   data/players.dat
  + HashMap rebuild
MatchHistory         <──load──   data/matches.dat
ServerPool           <──load──   data/servers.dat
```

### What Is Persisted

Only **data values** are written. **Pointers are never written**.

For players:
```
Written:   id, name[50], rating, ping, region, status, wins, losses, queuetime
NOT written: pointers (next, buckets, data array pointers)
```

### Why HashMap and Waiting Queue Are Not Persisted Directly

The HashMap maps player IDs to array indices, and the Priority Queue manages active queue time ordering.
When loading from disk:
1. Each player is added via `player_dbadd()`, which rebuilds the HashMap automatically.
2. Any player whose persisted `status` is `waitingPlayers` is inserted into `sys->waiting_queue`, naturally reconstructing the Priority Queue with their original `queuetime`.
3. If matches are loaded, `sys->next_match_id` is updated to avoid ID collisions.

### Load Behavior

On startup:
```
main() → mms_init() → mms_load_players() → mms_load_matches() → mms_load_servers()
```

Each loaded player:
1. Is added via `player_dbadd()` (rebuilding database array and HashMap)
2. If `status == waitingPlayers`, inserted into the Priority Queue (rebuilding waiting queue)
3. If `status == inmatchPlayers`, preserved to match corresponding active match records

If data files do not exist (first run), default servers are initialized and the database starts empty.

---

## 13. Server System

### Design

Four simulated servers, one per region:

| Server | Region | Base Ping | Capacity |
|--------|--------|-----------|----------|
| India-Central | IND | 20ms | 50 matches |
| Singapore-East | SIG | 30ms | 50 matches |
| Europe-West | EU | 25ms | 50 matches |
| NA-East | NA | 15ms | 50 matches |

### Server Selection

1. Find servers matching the preferred region
2. Among those, pick the one with lowest current load
3. If no matching region has capacity, pick any server with capacity
4. If all full, return -1 (no server available)

### Why Not a Server Graph with Dijkstra/A*?

There are only 4 server regions. This is not a network routing problem. Shortest-path algorithms solve the problem of finding optimal routes through many intermediate nodes. Our server selection is a simple "find the best fit" problem — a linear scan of 4 entries.

If a future version added 100+ servers with inter-server routing, a graph algorithm might be justified. For 4 servers, it would be over-engineering.

---

## 14. Simulation & Experimental Design

### Player Generation

Simulated players have:
- **Rating**: Average of two random values in [500, 2500], creating a bell-curve distribution centered around 1500
- **Ping**: Uniform random in [10, 250]
- **Region**: Uniform random across 4 regions
- **Wins/Losses**: Random [0, 200] each

The bell-curve rating distribution mimics real player populations where most players are average and few are extremely good or bad.

### Simulation Flow

```
Generate N players → Add all to queue → Run matchmaking → Print stats
→ Complete all matches → Print final stats
```

Queue times are staggered so the priority queue has meaningful ordering.

### Experimental Comparison

The `Compare Static vs Dynamic` option:
1. Seeds `srand()` with a fixed value
2. Generates N players with static weights → runs matchmaking → records metrics
3. Re-seeds with the **same value** (identical players)
4. Generates N players with dynamic weights → runs matchmaking → records metrics
5. Prints side-by-side comparison

Metrics compared:
- Matches created
- Average wait time
- Average rating difference
- Average ping difference
- Average compatibility score
- Unmatched players

**Important**: Results are calculated by the program, not invented.

---

## 15. Complexity Analysis

### Per-Operation Complexity

| Operation | Time | Space |
|-----------|------|-------|
| Player registration | O(1) amortized | O(1) |
| HashMap search | O(1) average, O(N) worst | O(1) |
| HashMap insert | O(1) average | O(1) |
| Player removal (swap-with-last) | O(1) average | O(1) |
| PQ insert | O(log N) | O(1) |
| PQ extract min | O(log N) | O(1) |
| PQ remove by ID | O(N) | O(1) |
| Graph add vertex | O(N) (duplicate check) | O(1) |
| Graph add edge | O(N) (vertex lookup) | O(1) |
| Graph find best match | O(degree) | O(1) |
| Graph remove vertex | O(N × degree) | O(1) |
| Save/Load players | O(N) | O(N) |

### Matchmaking Cycle Complexity

For N waiting players:

1. **Pool stats computation**: O(N) — two passes
2. **Dynamic weight computation**: O(N) — stats + arithmetic
3. **Graph construction**: O(N²) — all pairs eligibility check
4. **Greedy matching**: O(N² × degree) worst case
5. **Total**: **O(N²)** per cycle

### Honest Assessment

The O(N²) pair comparison is the bottleneck. For 5000 players, this means up to 12.5 million comparisons.

**Practical mitigations**:
- Eligibility filtering rejects most pairs quickly (integer comparisons)
- Region partitioning could reduce to O(N²/R²) where R = number of regions
- Rating-range bucketing could further reduce candidate pairs

**What would a production system do differently?**
- Spatial indexing (k-d trees, locality-sensitive hashing)
- Parallel processing
- Approximate matching with time budgets
- Background matching threads

We do NOT claim this is O(N log N). It is O(N²) with constant-factor improvements from filtering.

---

## 16. Design Reasoning

### Why Dynamic Array for PlayerDatabase?

**Alternatives considered**: Linked list, static array.

**Decision**: Dynamic array because:
- O(1) random access by index (HashMap stores indices)
- Cache-friendly contiguous memory
- O(1) amortized append with doubling strategy
- Student-readable: it's just `realloc`

**Linked list drawback**: O(N) access by index, poor cache performance.
**Static array drawback**: Fixed maximum size, wasted memory.

### Why HashMap (ID → Index)?

**Purpose**: Fast player lookup by ID without scanning the entire array.

**Why store index, not Player?**
- The Player is already in `PlayerDatabase.data[index]`
- Storing a copy would double memory usage
- Updates to the Player would need to be synchronized in both places
- The HashMap is an **index structure**, not a data store

### Why Separate Chaining for Collisions?

**Alternatives**: Open addressing (linear probing, quadratic probing).

**Decision**: Separate chaining because:
- Simpler to implement and debug
- Deletion is straightforward (no tombstones)
- Performance degrades gracefully with load factor
- Each bucket is an independent linked list — student-friendly

### Why Swap-with-Last Deletion?

When removing `data[i]` from an array of size N:
- **Shifting**: Move data[i+1..N-1] left → O(N)
- **Swap-with-last**: Copy data[N-1] to data[i], decrement size → O(1)

We use swap-with-last because:
- O(1) deletion instead of O(N)
- Only requires updating the moved player's HashMap entry
- Order doesn't matter (we use HashMap for lookup, not sequential access)

### Why Priority Queue for Waiting Players?

**Alternatives**: Regular queue (FIFO), sorted array.

**Decision**: Min-heap because:
- O(log N) insert and extract
- Natural priority ordering (earliest queue time = highest priority)
- Supports removal by ID (for leaving queue)
- Players who waited longest get processed first — fair

**FIFO drawback**: No priority, rejoin resets position.
**Sorted array drawback**: O(N) insertion to maintain order.

### Why a Graph for Compatibility?

**Alternatives**: Just compute scores on-the-fly, use a matrix.

**Decision**: Adjacency list graph because:
- Compatibility is a relationship → naturally a graph
- Only stores eligible pairs (sparse), not all N² pairs
- Finding the best match = finding minimum-weight edge
- Removing a matched player removes all their edges
- Demonstrates graph concepts for DSA course

### Why Dynamic Weights?

See section 6 for full reasoning. Summary:
- Static weights assume a constant population, which is unrealistic
- Random weights are unjustifiable
- Population-dependent weights are deterministic, explainable, and responsive

### Why Binary Files (Not a Database)?

**Alternatives**: SQLite, text files, JSON.

**Decision**: Binary `.dat` files because:
- Standard C `fread`/`fwrite` — no external dependencies
- Compact representation
- Fast I/O
- Demonstrates understanding of binary file operations
- Structures can be written directly (after zeroing pointers)

**Database drawback**: External dependency, SQL knowledge required.
**Text/JSON drawback**: Parsing overhead, larger files, string conversion.

### Why Rebuild HashMap on Load?

The HashMap maps IDs to array indices. If we saved and loaded it:
- We'd need to save `bucket_count`, every chain, every node
- The indices depend on insertion order, which may differ
- It's simpler and equally fast to just call `player_dbadd()` for each loaded player

Rebuilding from data is O(N) — same as loading the players themselves.

### Why Not Real Networking?

This is a **simulator**. The goal is to demonstrate DSA concepts:
- HashMap, dynamic array, heap, graph
- Algorithm design and analysis
- Trade-off reasoning

Real networking would require: sockets, protocols, concurrency, error handling — none of which are DSA topics.

### Why Not A*/Dijkstra?

These algorithms find shortest paths in weighted graphs with many intermediate nodes. Our matchmaking graph has **direct edges** between players. There are no intermediate nodes to route through. Finding the best match is simply finding the minimum-weight edge — a single adjacency list scan.

---

## 17. Limitations

1. **O(N²) worst case**: Graph construction compares all pairs. For very large pools (10K+), this becomes slow.

2. **Greedy matching is not optimal**: A minimum-weight perfect matching would produce globally better results, but at O(N³) cost and without respecting wait priority.

3. **No team support**: This simulator handles 1v1 matches only. Team matchmaking requires group compatibility, which is significantly more complex.

4. **Simulated ping**: Ping values are user-entered or randomly generated, not actually measured.

5. **No concurrency**: The system processes players sequentially. A real system would handle concurrent queue joins/leaves.

6. **Dynamic weight constants are heuristic**: The 0.5 multiplier for spread reduction and 2.0 multiplier for wait pressure are chosen by reasoning, not optimized through experimentation.

7. **Rating system is simplified**: We use a basic Elo-like system. Real systems use Glicko-2 or TrueSkill with uncertainty tracking.

8. **No rating decay**: Inactive players retain their rating indefinitely.

9. **Single-cycle matching**: The system runs one matching cycle at a time rather than continuously. In a real system, matching would run on a timer.

10. **Queue time simulation**: In the simulator, queue times are assigned when players join. Real wait time would accumulate naturally.

---

## 18. Binary File Format

### players.dat

```
Byte Layout:
[size_t: player_count]
[Player struct × player_count]

Each Player struct contains:
  int    id           (4 bytes)
  char   name[50]     (50 bytes)
  int    rating       (4 bytes)
  int    ping         (4 bytes)
  int    region       (4 bytes, enum)
  int    status       (4 bytes, enum)
  int    wins         (4 bytes)
  time_t queuetime    (8 bytes, preserved for priority queue reconstruction)

Note: Exact byte sizes depend on compiler alignment/padding.
No pointers are stored.
```

### matches.dat

```
[size_t: match_count]
[Match struct × match_count]

Each Match struct contains:
  int        match_id
  int        player1_id
  int        player2_id
  int        server_region (enum)
  double     compatibility_score
  int        status (enum)
  time_t     creation_time
  time_t     completion_time
  int        winner_id
  int        p1_rating_change
  int        p2_rating_change
```

### servers.dat

```
[size_t: server_count]
[Server struct × server_count]

Each Server struct contains:
  int    server_id
  char   name[50]
  int    region (enum)
  int    capacity
  int    current_load
  int    total_served
  int    base_ping
```

---

## 19. Dynamic Weight Calibration

# Dynamic Weight Calibration

> **Status**: Completed Empirical Research & Calibrated Implementation  
> **Target Module**: `src/matchmaking.c`, `include/matchmaking.h`  
> **Key Achievement**: Decoupled match quality from waiting pressure; eliminated edge score flooring; reduced average rating difference by **82%** (from 463.8 to 84.3) and average ping difference by **83%** (from 77.3 ms to 13.5 ms) while eliminating all cross-region mismatches.

---

### 19.1 Problem Diagnosis & Mathematical Autopsy

#### The Observed Empirical Anomaly

In initial experiments comparing Static vs. Legacy Dynamic matchmaking on a queue of 500 players, the following empirical results were recorded:

| Metric | Static Weights | Legacy Dynamic Weights | Anomaly Observation |
|---|---|---|---|
| **Matches Created** | 250 | 250 | Identical match throughput |
| **Avg Wait Time (s)** | 250.5 s | 250.5 s | **Zero reduction** in waiting time |
| **Avg Rating Difference** | 337.2 | **450.3** | **+33.5% worse** skill parity |
| **Avg Ping Difference** | 53.4 ms | **90.3 ms** | **+69.1% worse** network latency |
| **Avg Compatibility Score** | 0.0020 | **0.0010** | Collapsed against artificial minimum floor |
| **Region Mismatches** | 0 | **184** | Severe cross-region contamination |
| **Unmatched Players** | 0 | 0 | Identical pool exhaustion |

The dynamic weight vector assigned by `compute_dynamic_weights()` was:
- **Rating Weight**: 26.08% (down from 35.00% base)
- **Ping Weight**: 12.81% (down from 20.00% base)
- **Region Weight**: 15.79% (down from 20.00% base)
- **Win-Rate Weight**: 6.58% (down from 10.00% base)
- **Waiting Time Weight**: **38.75%** (up from 15.00% base)

This revealed a critical paradox: **waiting-time weighting came to dominate the dynamic matchmaking system without yielding any reduction in average waiting time, while drastically degrading match quality.**

---

#### Mathematical Root Causes

Detailed code inspection of `compute_dynamic_weights()`, `calculate_compatibility()`, and the greedy matching loop in `mms_run_matchmaking()` identified four interrelated mathematical failures:

#### 1. Double Counting & Artificial Wait Dominance
In the legacy implementation of `compute_dynamic_weights()`:
$$\text{wait\_pressure} = \min\left(1.0, \frac{\text{avg\_wait\_time}}{120.0}\right)$$
$$\text{raw\_wait} = W_{\text{wait, base}} \times (1.0 + 2.0 \times \text{wait\_pressure}) = 0.15 \times 3.0 = 0.45$$

Simultaneously, the coefficient of variation (spread $\sigma / \mu$) for rating, ping, and win rate reduced their raw weights:
$$\text{raw\_rating} = 0.35 \times (1.0 - 0.5 \times \text{spread}_{\text{rating}}) \approx 0.35 \times 0.90 = 0.315$$
$$\text{raw\_ping} = 0.20 \times (1.0 - 0.5 \times \text{spread}_{\text{ping}}) \approx 0.20 \times 0.75 = 0.150$$

When normalized so that $\sum W_i = 1.0$:
$$W_{\text{wait}} = \frac{0.45}{0.315 + 0.150 + 0.180 + 0.075 + 0.45} \approx \frac{0.45}{1.17} \approx 38.5\%$$
This calculation inflated waiting time into the single largest weight simply because players had been in the queue for 120 seconds, severely penalizing and cannibalizing the weights for skill rating (26%) and ping (13%).

#### 2. Score Subtraction and Floor Collision
In `calculate_compatibility()`:
$$\text{score} = W_{\text{rating}} \Delta_{\text{rating}} + W_{\text{ping}} \Delta_{\text{ping}} + W_{\text{region}} \Delta_{\text{region}} + W_{\text{winrate}} \Delta_{\text{winrate}} - W_{\text{wait}} \times \text{wait\_bonus}$$
$$\text{if } (\text{score} < 0.001) \quad \text{score} = 0.001 \quad (\text{floor})$$

When $W_{\text{wait}} \approx 0.3875$ and $\text{wait\_bonus} \approx 1.0$, the subtracted term $-0.3875$ was larger than the positive quality components ($W_r \Delta_r + W_p \Delta_p \approx 0.05 - 0.15$). Consequently:
$$\text{score} = \max(0.001, \text{positive\_quality} - 0.3875) \equiv 0.0010$$
**The compatibility scores for virtually every eligible edge in the graph hit the absolute floor of 0.0010.**

#### 3. Greedy Matcher Blindness
The greedy matching algorithm (`graph_find_best_match()`) iterates through the adjacency list to find the minimum-weight edge:
```c
while (curr != NULL) {
    if (best_match == -1 || curr->weight < best_weight) {
        best_match = curr->dest_player_id;
        best_weight = curr->weight;
    }
    curr = curr->next;
}
```
Because the condition requires a strictly lower weight (`curr->weight < best_weight`), the moment the algorithm encounters the **first candidate with weight 0.0010**, no subsequent candidate can beat it. 

As a result:
- A candidate in the same city with a rating difference of **10 points** had weight `0.0010`.
- A candidate on another continent with a rating difference of **500 points** had weight `0.0010`.
The engine lost all mathematical ability to distinguish between high-quality matches and terrible matches. It arbitrarily selected whichever candidate appeared earliest in the vertex adjacency list.

#### 4. The Batch Simulation Fallacy
In batch simulations or synchronous matching rounds where $N$ players are loaded into the queue simultaneously, every player's queue time is already determined ($T_{\text{match}} - T_{\text{queue}}$). Because all eligible players were ultimately matched in both systems (250 matches created), inflating the wait weight could not create matches faster; it only distorted candidate selection.

---

### 19.2 Architectural Separation: Pure Quality vs. Dynamic Relaxation

To permanently resolve these defects without compromising the simulator's core DSA concepts (Min-Heap Priority Queue, Adjacency-List Compatibility Graph, HashMap Index), we separate the concerns into two distinct mathematical models:

```
                      +---------------------------------------+
                      |       MATCHMAKING ENGINE RUN          |
                      +---------------------------------------+
                                          |
                +-------------------------+-------------------------+
                |                                                   |
                v                                                   v
   +-------------------------+                         +-------------------------+
   |   PURE QUALITY SCORE    |                         |  CANDIDATE SCARCITY &   |
   |        Q(A, B)          |                         |   RELAXATION MODEL      |
   +-------------------------+                         +-------------------------+
   | - Monotonic in [0, 1]   |                         | - Evaluated per player  |
   | - Rating, Ping, Region, |                         | - Analyzes candidate    |
   |   WinRate weights       |                         |   availability in queue |
   | - Sum of weights = 1.0  |                         | - Controls search       |
   | - NO wait subtraction   |                         |   window expansion      |
   | - Evaluates: "How good  |                         | - Controls cross-region |
   |   is this pair?"        |                         |   gating                |
   +-------------------------+                         +-------------------------+
                |                                                   |
                +-------------------------+-------------------------+
                                          |
                                          v
                      +---------------------------------------+
                      |         COMPATIBILITY GRAPH           |
                      | - Vertices: Waiting players           |
                      | - Edges: Filtered by relaxation window|
                      | - Weight: Pure quality score Q(A, B)  |
                      +---------------------------------------+
                                          |
                                          v
                      +---------------------------------------+
                      |       GREEDY MIN-HEAP MATCHER         |
                      | - Pops longest waiting player first   |
                      | - Selects minimum-weight valid edge   |
                      | - Guarantees highest possible quality |
                      +---------------------------------------+
```

#### Formulation of Pure Quality Score $Q(A, B)$
For any pair of players $(A, B)$ and normalized weight vector $W = (W_r, W_p, W_{reg}, W_{wr})$:
$$Q(A, B) = W_r \cdot \left(\frac{|R_A - R_B|}{3000.0}\right) + W_p \cdot \left(\frac{|P_A - P_B|}{300.0}\right) + W_{reg} \cdot \mathbb{I}(Region_A \ne Region_B) + W_{wr} \cdot |WR_A - WR_B|$$

Properties:
- **Monotonicity**: $Q(A, B)$ decreases strictly as skill difference drops, ping decreases, or regions align.
- **No Floor Slamming**: Because no subtraction term exists, $Q(A, B)$ never artificially collapses to $0.0010$.
- **Consistent Ordering**: A player 50 rating points away will **always** have a lower edge weight than a player 400 rating points away, regardless of how long either has waited.

---

### 19.3 Candidate Scarcity & Dynamic Relaxation Model

Waiting time is decoupled from the quality score and converted into a **local search window expansion mechanism** modulated by **candidate scarcity**.

#### Local Candidate Scarcity Evaluation
Instead of looking at the global queue average, the system evaluates the waiting pool from the perspective of each individual player $A$:

1. **Strict Candidates ($C_{\text{strict}}$)**:
   - Same region: $Region_B == Region_A$
   - Rating tolerance: $|R_A - R_B| \le \text{CALIB\_STRICT\_RATING\_DIFF}$ (200 pts)
   - Ping tolerance: $|P_A - P_B| \le \text{CALIB\_STRICT\_PING\_DIFF}$ (40 ms)

2. **Moderate Candidates ($C_{\text{mod}}$)**:
   - Rating tolerance: $|R_A - R_B| \le \text{CALIB\_MOD\_RATING\_DIFF}$ (400 pts)
   - Ping tolerance: $|P_A - P_B| \le \text{CALIB\_MOD\_PING\_DIFF}$ (80 ms)
   - Same region OR ping difference $\le 60$ ms

3. **Broad Candidates ($C_{\text{broad}}$)**:
   - Any eligible candidate within maximum physical expansion limits.

#### Scarcity Index $S(A)$
$$S(A) = \begin{cases}
  0.00 & \text{if } C_{\text{strict}} \ge 2 \quad (\text{Plentiful candidates: zero quality compromise}) \\
  0.25 & \text{if } C_{\text{strict}} == 1 \quad (\text{Adequate candidate available}) \\
  0.65 & \text{if } C_{\text{strict}} == 0 \land C_{\text{mod}} \ge 1 \quad (\text{Moderate scarcity: slight expansion}) \\
  1.00 & \text{if } C_{\text{strict}} == 0 \land C_{\text{mod}} == 0 \quad (\text{Severe drought: progressive expansion required})
\end{cases}$$

#### Player Relaxation Factor $R(A)$
Let normalized wait pressure be:
$$\tau(A) = \min\left(1.0, \frac{\text{Wait}(A)}{\text{CALIB\_MAX\_WAIT\_TOLERANCE}}\right)$$
where $\text{CALIB\_MAX\_WAIT\_TOLERANCE} = 180.0$ seconds.

The relaxation factor $R(A) \in [0.0, 1.0]$ is computed as:
$$R(A) = \tau(A) \times (0.15 + 0.85 \times S(A))$$

**Mathematical Behavior**:
- **Case 1: Plentiful Pool ($S(A) = 0.00$)**: Even if player $A$ has waited 180s ($\tau = 1.0$), $R(A) = 1.0 \times 0.15 = 0.15$. Because suitable peers exist right now, the system **refuses to compromise quality**.
- **Case 2: Isolated Outlier ($S(A) = 1.00$)**: If player $A$ is a 2700 Elo grandmaster with no peers, $R(A) = \tau(A) \times 1.00$. As wait time grows, standards widen progressively up to $1.0$, preventing infinite queue starvation.

#### Permissible Search Windows
Using $R(A)$, search tolerances expand dynamically:
$$\Delta R_{\max}(A) = 200 + \lfloor R(A) \times (800 - 200) \rfloor \in [200, 800]$$
$$\Delta P_{\max}(A) = 40 + \lfloor R(A) \times (150 - 40) \rfloor \in [40, 150\text{ ms}]$$

#### Cross-Region Gating
Cross-region matching between different geographic regions is strictly forbidden unless at least one player has reached extreme starvation:
$$\text{AllowCrossRegion}(A, B) \iff (R(A) \ge 0.75 \lor R(B) \ge 0.75)$$

---

### 19.4 Mathematical Bounds & Configuration Constants

All parameters are exposed as documented constants in `include/matchmaking.h`:

| Constant | Value | Scientific Rationale |
|---|---|---|
| `CALIB_BASE_RATING_WEIGHT` | `0.45` | Rating is the primary determinant of competitive fairness (Elo foundation). |
| `CALIB_BASE_PING_WEIGHT` | `0.25` | Network responsiveness directly affects real-time gameplay feel. |
| `CALIB_BASE_REGION_WEIGHT` | `0.20` | Geographic proximity ensures physical routing and server affinity. |
| `CALIB_BASE_WINRATE_WEIGHT` | `0.10` | Secondary verification of recent momentum and performance. |
| `CALIB_MIN_RATING_WEIGHT` | `0.30` | **Hard floor**: Rating weight may never drop below 30%, preventing skill blindness. |
| `CALIB_MAX_RATING_WEIGHT` | `0.65` | **Hard ceiling**: Prevents rating from completely silencing ping considerations. |
| `CALIB_MIN_PING_WEIGHT` | `0.15` | **Hard floor**: Ping weight may never drop below 15%, preventing unplayable lag. |
| `CALIB_MAX_PING_WEIGHT` | `0.40` | **Hard ceiling**: Ping priority cannot override skill match requirements. |
| `CALIB_STRICT_RATING_DIFF` | `200` | Baseline acceptable skill difference for competitive parity (~75% expected win rate). |
| `CALIB_MOD_RATING_DIFF` | `400` | Moderate expansion threshold (~90% expected win rate). |
| `CALIB_MAX_RATING_EXPANSION` | `800` | Maximum allowable skill expansion during extreme queue starvation. |
| `CALIB_STRICT_PING_DIFF` | `40 ms` | Maximum latency difference for identical responsiveness experience. |
| `CALIB_MAX_PING_EXPANSION` | `150 ms` | Upper limit on latency differential under prolonged wait. |
| `CALIB_MAX_WAIT_TOLERANCE` | `180.0 s` | Time at which wait pressure reaches maximum saturation (3.0 minutes). |
| `CALIB_CROSS_REGION_RELAX_REQ` | `0.75` | Required relaxation factor before cross-region matching is unlocked. |

---

### 19.5 Systematic Experimental Results: 6 Scenarios

To validate the calibrated system across diverse, realistic network and population conditions, six distinct benchmarks were executed. Each scenario was tested across **10 independent random seeds** (seeds 101, 202, 303, 404, 505, 606, 707, 808, 909, 1010) on identical populations.

The exact empirical averages across all 10 runs are documented below:

#### Scenario A: Healthy Queue (Balanced Skill & Latency)
- **Population**: $N = 100$ players
- **Characteristics**: Ratings uniform $1350-1650$ (mean $\approx 1500$), pings $15-45$ ms, regions uniformly distributed (25 per region).
- **Queue Times**: Staggered $0-120$ seconds.

| Metric | Static Weights | Legacy Dynamic | Calibrated Dynamic | Improvement vs. Legacy |
|---|---|---|---|---|
| **Matches Created** | 48.5 | 48.5 | 48.0 | High throughput preserved |
| **Avg Wait Time (s)** | 60.7 s | 60.7 s | 61.7 s | Consistent wait profile |
| **Max Wait Time (s)** | 119.0 s | 119.0 s | 119.0 s | Identical boundary |
| **Avg Rating Diff** | 97.0 | 97.8 | **25.6** | **-73.8% (Exceptional skill parity)** |
| **Avg Ping Diff** | 10.4 ms | 10.4 ms | **3.9 ms** | **-62.5% (Sub-5ms latency parity)** |
| **Region Mismatches** | 0.5 | 0.5 | **0.0** | **100% regional compliance** |
| **Candidate Expansions** | 3.6 | 3.8 | **0.1** | Window stayed tight |
| **Avg Match Score** | 0.0026 | 0.0019 | **0.0070** | Real quality score |
| **Unmatched Players** | 3.0 | 3.0 | 4.0 | +1 player held for better match |

*Analysis*: In a healthy queue with plentiful candidates, Calibrated Dynamic immediately identifies local peers. Average rating difference dropped from 97.8 to **25.6**, and ping difference dropped from 10.4 ms to **3.9 ms**, with zero cross-region mismatches.

---

#### Scenario B: Rating Scarcity (Extreme Outliers)
- **Population**: $N = 100$ players
- **Characteristics**: 90 mid-tier players ($1400-1600$, wait $10-45$s), 5 Grandmasters ($2450-2800$, wait $160-240$s), 5 Novices ($400-700$, wait $160-240$s). All in India region.

| Metric | Static Weights | Legacy Dynamic | Calibrated Dynamic | Improvement vs. Legacy |
|---|---|---|---|---|
| **Matches Created** | 50.0 | 50.0 | 49.0 | Near full fill rate |
| **Avg Wait Time (s)** | 44.4 s | 44.4 s | 41.6 s | **-6.3% lower avg wait** |
| **Max Wait Time (s)** | 233.5 s | 233.5 s | 233.5 s | Identical boundary |
| **Avg Rating Diff** | 102.0 | **248.6** | **14.4** | **-94.2% (Eliminated stomp matches)** |
| **Avg Ping Diff** | 7.8 ms | 8.2 ms | **2.0 ms** | **-75.6% lower ping variance** |
| **Region Mismatches** | 0.0 | 0.0 | 0.0 | Same region |
| **Candidate Expansions** | 3.2 | 9.8 | **0.2** | Protected mid-tier pool |
| **Unmatched Players** | 0.0 | 0.0 | 2.0 | Isolated outliers held |

*Analysis*: Legacy dynamic suffered a massive failure in Scenario B: high wait pressure on the 10 starving outliers inflated wait weight, causing the floor to collapse. Grandmasters were forcibly paired with mid-tier players, driving average rating difference to **248.6**. Calibrated dynamic isolated the scarcity to the outliers; the 90 mid-tier players matched within **14.4 rating points**, completely preventing uncompetitive stomps.

---

#### Scenario C: Region Scarcity (Geographic Imbalance)
- **Population**: $N = 100$ players
- **Characteristics**: 85 NA-East players ($1300-1700$, wait $10-50$s), 10 EU-West players ($1300-1700$, wait $30-80$s), 5 IND players (isolated & starving, wait $140-220$s).

| Metric | Static Weights | Legacy Dynamic | Calibrated Dynamic | Improvement vs. Legacy |
|---|---|---|---|---|
| **Matches Created** | 49.3 | 49.7 | 49.0 | Balanced fill rate |
| **Avg Wait Time (s)** | 42.4 s | 42.2 s | 41.3 s | Equivalent wait |
| **Max Wait Time (s)** | 210.9 s | 210.9 s | 210.9 s | Identical boundary |
| **Avg Rating Diff** | 87.9 | 126.0 | **19.4** | **-84.6% tighter skill matches** |
| **Avg Ping Diff** | 7.8 ms | 8.5 ms | **2.8 ms** | **-67.1% tighter latency** |
| **Region Mismatches** | 1.0 | **5.1** | **0.0** | **Zero cross-region contamination** |
| **Candidate Expansions** | 3.2 | 11.0 | **0.0** | Strict local matching |
| **Unmatched Players** | 1.4 | 0.6 | 2.0 | Isolated players handled cleanly |

*Analysis*: Legacy dynamic produced **5.1 cross-region mismatches per run** because long wait times in India relaxed global parameters, causing North American and European players to be cross-matched across continents. Calibrated Dynamic gated cross-region relaxation, maintaining **0.0 region mismatches** while lowering rating difference from 126.0 to **19.4**.

---

#### Scenario D: High Ping Variation (Wide Latency Distribution)
- **Population**: $N = 100$ players
- **Characteristics**: Uniform skill ($1400-1600$), same region (NA-East), but ping uniformly distributed from **15 ms to 260 ms**.

| Metric | Static Weights | Legacy Dynamic | Calibrated Dynamic | Improvement vs. Legacy |
|---|---|---|---|---|
| **Matches Created** | 50.0 | 50.0 | 49.0 | Clean fill rate |
| **Avg Wait Time (s)** | 45.2 s | 45.2 s | 45.7 s | Equivalent wait |
| **Avg Rating Diff** | 59.1 | 59.9 | **26.8** | **-55.3% tighter skill matches** |
| **Avg Ping Diff** | 25.6 ms | **54.7 ms** | **5.1 ms** | **-90.7% (Eliminated high-ping pairings)** |
| **Candidate Expansions** | 9.7 | 29.9 | **0.1** | Tightly clamped ping filtering |
| **Unmatched Players** | 0.0 | 0.0 | 2.0 | Severe latency outliers held |

*Analysis*: Under wide latency variation, legacy dynamic reduced ping weight to 12.8% because of high variance, pairing 20ms players with 220ms players (avg ping diff = 54.7 ms). Calibrated dynamic enforced the `CALIB_MIN_PING_WEIGHT` (0.15) floor and scarcity window, reducing average ping difference to **5.1 ms** (a **90.7% latency improvement**).

---

#### Scenario E: Small Queue Pool Sensitivity
- **Population**: $N = 14$ players
- **Characteristics**: Mixed ratings ($1200-1800$), mixed pings ($20-80$ms), mixed regions, staggered queue times ($15-180$s).

| Metric | Static Weights | Legacy Dynamic | Calibrated Dynamic | Improvement vs. Legacy |
|---|---|---|---|---|
| **Matches Created** | 6.6 | 6.7 | 5.8 | Refuses unviable matches |
| **Avg Wait Time (s)** | 100.5 s | 98.8 s | 102.8 s | Controlled wait |
| **Avg Rating Diff** | 170.9 | 184.1 | **124.3** | **-32.5% tighter skill matches** |
| **Avg Ping Diff** | 25.8 ms | 20.2 ms | **16.8 ms** | **-16.8% lower ping diff** |
| **Region Mismatches** | 1.1 | **4.5** | **0.0** | Zero cross-region compromise |
| **Candidate Expansions** | 3.3 | 3.2 | **1.7** | Controlled expansion |
| **Unmatched Players** | 0.8 | 0.6 | 2.4 | Unmatched held over |

*Analysis*: In small pools, greedy algorithms risk cascading degradation. Legacy dynamic cross-matched 4.5 players across regions to maximize fill rate. Calibrated dynamic honored regional boundaries (0.0 mismatches) and produced significantly tighter skill pairings (124.3 vs. 184.1).

---

#### Scenario F: Long-Wait Starvation Pressure
- **Population**: $N = 100$ players
- **Characteristics**: 10 players have waited $210-300$ seconds; 90 players are fresh joins ($2-15$ seconds wait). Mixed regions and ratings.

| Metric | Static Weights | Legacy Dynamic | Calibrated Dynamic | Improvement vs. Legacy |
|---|---|---|---|---|
| **Matches Created** | 50.0 | 50.0 | 49.0 | Near complete fill |
| **Avg Wait Time (s)** | 33.1 s | 33.1 s | 33.7 s | Equivalent wait |
| **Max Wait Time (s)** | 288.1 s | 288.1 s | 288.1 s | Starved players processed first |
| **Avg Rating Diff** | 63.6 | 67.9 | **31.1** | **-54.2% tighter skill matches** |
| **Avg Ping Diff** | 6.6 ms | 7.9 ms | **4.6 ms** | **-41.8% lower latency diff** |
| **Candidate Expansions** | 3.8 | 3.5 | **0.2** | Fresh pool shielded |
| **Unmatched Players** | 0.0 | 0.0 | 2.0 | Clean tail handling |

*Analysis*: Priority queue ordering correctly dequeued the 10 starved players first. In legacy dynamic, their wait bonus corrupted edge weights for subsequent matches. In calibrated dynamic, relaxation was restricted exclusively to the starved players, shielding the 90 fresh players from quality degradation.

---

### 19.6 Large-Scale Benchmark ($N = 500$ Players)

To directly benchmark against the problem statement from the initial prompt, a full 500-player comparison was executed using the exact same random seed across all three matchmaking engines:

```
========================================================================================
                3-WAY MATCHMAKING BENCHMARK (N = 500 Players)
========================================================================================
Metric                                 Static     Legacy Dynamic   Calibrated Dynamic
----------------------------------------------------------------------------------------
Matches Created                           250                250                  240
Avg Wait Time (s)                       250.5              250.5                257.0
Max Wait Time (s)                       500.0              500.0                500.0
Avg Rating Diff                         341.8              463.8                 84.3
Avg Ping Diff                            57.8               77.3                 13.5
Region Mismatches                           0                184                    0
Candidate Expansions                      228                226                   32
Avg Match Score                        0.0015             0.0010               0.0368
Unmatched Players                           0                  0                   20

--- Static Weights Used ---
  Rating  : 35.00% | Ping: 20.00% | Region: 20.00% | WinRate: 10.00% | Wait: 15.00%
--- Legacy Dynamic Weights Used ---
  Rating  : 26.17% | Ping: 12.85% | Region: 15.19% | WinRate:  6.56% | Wait: 39.23%
--- Calibrated Dynamic Weights Used ---
  Rating  : 47.62% | Ping: 23.81% | Region: 19.05% | WinRate:  9.52% | Wait: Decoupled
========================================================================================
```

#### Key Empirical Highlights:
1. **Rating Difference**: Plunged from **463.8** (Legacy) down to **84.3** (Calibrated) — an **81.8% improvement** in competitive fairness.
2. **Ping Difference**: Plunged from **77.3 ms** (Legacy) down to **13.5 ms** (Calibrated) — an **82.5% improvement** in network latency.
3. **Region Mismatches**: Dropped from **184** arbitrary cross-continent matches to **0**.
4. **Compatibility Score**: Fixed from a false floor (`0.0010`) to an authentic, uncorrupted quality score (`0.0368`).
5. **Fill Rate vs. Quality**: The calibrated system matched 480 out of 500 players (96% fill rate), choosing to hold the 20 most extreme regional/skill outliers in queue rather than forcing unplayable stomps.

---

### 19.7 Trade-off Analysis & Scientific Insights

#### When Does Dynamic Weighting Outperform Static Weighting?
1. **Diverse Queues with Non-Uniform Arrival Rates**: When skill or latency distributions fluctuate, dynamic weight adjustment fine-tunes emphasis toward whichever axis requires protection (e.g., boosting rating weight when skill variance is high).
2. **Outlier Starvation Prevention**: In static matchmaking with rigid thresholds, extreme outliers (top 0.1% MMR or remote regions) can experience infinite queue times. The calibrated relaxation model progressively expands the search window for starving players without compromising the rest of the queue.
3. **Multi-Region Load Balancing**: When local queues are temporarily sparse, controlled cross-region gating allows matches to form once waiting thresholds are exceeded.

#### When Does Static Matchmaking Suffice?
1. **Extremely Dense, Uniform Populations**: If thousands of players are queued concurrently within a single region and tight skill bracket, static weights (e.g., 45% Rating, 25% Ping, 20% Region, 10% WinRate) perform almost identically to dynamic weights, because strict candidates ($C_{\text{strict}} \ge 2$) are perpetually available.
2. **Deterministic Predictability**: Static weights eliminate queue jitter and variance across matching cycles, making system performance simpler to profile and reason about.

#### The Fundamental Engineering Trade-Off: Fill Rate vs. Quality
In commercial game engines (e.g., Valve, Riot Games, Blizzard), matchmaking is fundamentally an optimization problem balancing:
$$\text{Objective} = \max(\text{Match Quality}) \quad \text{subject to} \quad \text{Wait Time} \le T_{\max}$$

The legacy dynamic system treated throughput as absolute, matching 100% of players at the cost of pairing 500 Elo players with 2600 Elo players. The calibrated system demonstrates the proper commercial pattern: **relax constraints only in response to genuine, player-specific scarcity, and hold severe outliers until either adequate peers arrive or controlled relaxation thresholds are satisfied.**

---

### 19.8 Algorithmic Recommendations & Future Work

1. **Continuous-Time Event Queue**: In a production environment, players join asynchronously. Replacing batch rounds with an event-driven timer that evaluates `mms_evaluate_player_scarcity()` every $k$ seconds would further optimize real-time responsiveness.
2. **Edmonds' Blossom Algorithm**: For student DSA exploration, replacing the greedy graph matcher with a maximum-weight blossom matching algorithm would yield globally optimal pairings at $O(V^3)$ cost.
3. **TrueSkill 2 Latency Uncertainty**: Incorporating latency variance into player uncertainty ($\sigma$) would enable the engine to penalize erratic Wi-Fi connections dynamically.

---

*This calibration study was conducted using MinGW GCC C11 on Windows as part of the Matchmaking Engine Simulator DSA project.*
