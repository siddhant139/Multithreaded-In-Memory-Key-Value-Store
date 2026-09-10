# kvstore — Multithreaded In-Memory Key-Value Store

A Redis-inspired in-memory key-value store built from scratch in **C++17**. Features concurrent client handling via a custom thread pool, LRU eviction, TTL-based key expiry, and a TCP server using raw POSIX sockets.

Built as a systems programming project to demonstrate OS-level concurrency, networking, memory management, and data structure design.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    TCP Clients                          │
│              (telnet / nc / benchmark.py)               │
└──────────┬──────────┬──────────┬────────────────────────┘
           │          │          │
           ▼          ▼          ▼
┌─────────────────────────────────────────────────────────┐
│              TCP Server (POSIX Sockets)                 │
│         accept() loop → dispatch to thread pool         │
│                                                         │
│  ┌───────────────────────────────────────────────────┐  │
│  │           Thread Pool (N worker threads)          │  │
│  │    Producer-consumer queue + condition variable   │  │
│  └───────────────────────┬───────────────────────────┘  │
│                          │                              │
│  ┌───────────────────────▼───────────────────────────┐  │
│  │             Command Parser & Handler              │  │
│  │        Text protocol → parse → route → execute    │  │
│  └───────────────────────┬───────────────────────────┘  │
│                          │                              │
│  ┌───────────────────────▼───────────────────────────┐  │
│  │          Thread-Safe KV Store                     │  │
│  │   ┌─────────────────────────────────────────┐     │  │
│  │   │  std::unordered_map + std::shared_mutex │     │  │
│  │   │  (concurrent reads / exclusive writes)  │     │  │
│  │   └─────────────────────────────────────────┘     │  │
│  │   ┌──────────────┐  ┌──────────────────────┐      │  │
│  │   │ LRU Eviction │  │   TTL Expiry Manager │      │  │
│  │   │ (list + map) │  │ (background sampling)│      │  │
│  │   └──────────────┘  └──────────────────────┘      │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

## Features

- **TCP Server** — Raw POSIX sockets (`socket`, `bind`, `listen`, `accept`) with `SO_REUSEADDR`
- **Thread Pool** — Fixed-size worker pool using `std::thread`, `std::mutex`, `std::condition_variable`
- **Reader-Writer Locking** — `std::shared_mutex` for concurrent GET operations, exclusive SET/DEL
- **LRU Eviction** — O(1) eviction using doubly-linked list + hash map (LeetCode #146 in production)
- **TTL Support** — Per-key expiry with lazy deletion on read + background purge thread (Redis-style probabilistic sampling)
- **AOF Persistence** — Append-only-file logging of writes with configurable fsync policy (always/everysec/no) and log replay on startup for **crash recovery**
- **Pipelining** — Clients can batch many commands per round-trip; the server drains all buffered commands before reading more (~4.7× throughput vs. one-at-a-time)
- **Dual Protocol** — Auto-detects inline text vs. RESP; wire-compatible with `redis-cli`
- **Graceful Shutdown** — Signal handling (SIGINT/SIGTERM) for clean resource cleanup
- **CLI Configuration** — Configurable port, thread count, max entries, and AOF settings
- **Stats Tracking** — Atomic counters for hits, misses, gets, sets, evictions

## Supported Commands

| Command | Syntax | Description |
|---------|--------|-------------|
| SET | `SET key value [EX seconds]` | Store key-value pair, optional TTL |
| GET | `GET key` | Retrieve value by key |
| DEL | `DEL key [key ...]` | Delete one or more keys |
| EXISTS | `EXISTS key` | Check if key exists (1/0) |
| KEYS | `KEYS` | List all keys |
| TTL | `TTL key` | Remaining TTL in seconds (-1 = no expiry, -2 = not found) |
| INFO | `INFO` | Server stats (uptime, keys, hits, misses, evictions) |
| PING | `PING` | Health check (returns PONG) |

## Build & Run

```bash
# Build
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -pthread -I src \
    src/*.cpp -o build/kvstore

# Run (defaults: port 6379, 4 threads, 100K max entries)
./build/kvstore

# Run with custom config
./build/kvstore -p 6399 -t 8 -m 50000

# Run with durability (append-only file + crash recovery)
./build/kvstore -p 6399 --aof data.aof --fsync everysec
#   --aof FILE      enable persistence at FILE (replayed on next startup)
#   --fsync POLICY  always | everysec | no   (durability vs. throughput)

# Or with CMake
mkdir build && cd build
cmake .. && make
./kvstore -p 6399
```

## Usage

```bash
# Terminal 1: Start server
./build/kvstore -p 6399

# Terminal 2: Connect with nc/telnet
nc localhost 6399
PING
# PONG
SET user:1 alice
# OK
SET session:abc token123 EX 300
# OK
GET user:1
# alice
TTL session:abc
# (integer) 299
INFO
# Server stats...
```

## Design Decisions

### Why `std::shared_mutex` (Reader-Writer Lock)?

A regular `std::mutex` would serialize all operations. In a key-value store, reads vastly outnumber writes (typical 80/20 split). `std::shared_mutex` allows multiple concurrent GET operations while ensuring exclusive access for SET/DEL. This is the same pattern used by production databases.

### Why a custom Thread Pool over thread-per-connection?

Thread-per-connection doesn't scale — creating/destroying OS threads has ~50μs overhead, and thousands of threads exhaust memory. A fixed thread pool with a task queue (producer-consumer pattern) amortizes thread creation cost and bounds resource usage.

### Why LRU over other eviction policies?

LRU (Least Recently Used) is the most common eviction policy in caches because it approximates optimal replacement under temporal locality. The implementation uses a doubly-linked list + hash map for O(1) complexity on all operations — the same approach as `LinkedHashMap` in Java and Redis's `allkeys-lru`.

### Why lazy + active TTL expiry?

- **Lazy expiry**: On every GET, check if the key has expired. Zero overhead for non-TTL keys.
- **Active expiry**: Background thread samples 20 random keys per second. If >25% are expired, repeat immediately. This is Redis's exact strategy — it prevents memory from being held by expired-but-unaccessed keys without scanning the entire keyspace.

### Dual protocol: inline text + RESP

The server auto-detects the client's protocol from the first byte of each request:

- **Inline text protocol** (`SET key value\r\n`) — human-readable, testable with `telnet` or `nc` without special tooling.
- **RESP** (REdis Serialization Protocol) — the binary-safe, length-prefixed wire format used by real Redis. A RESP request looks like `*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n`. This makes the server **wire-compatible with `redis-cli`** and standard Redis client libraries.

Internally the command handler is protocol-agnostic: it returns a structured `Reply` object (simple string, error, integer, bulk string, nil, or array), which the server serializes into whichever protocol the client is speaking (`Reply::toResp()` or `Reply::toInline()`). This cleanly decouples command logic from wire encoding.

```
Client byte starts with '*'  → RESP framing  → Reply::toResp()   (redis-cli)
Otherwise                    → line framing  → Reply::toInline() (nc / telnet)
```

### Persistence: append-only file (AOF)

The store is in-memory, so a crash would lose everything. To make it **durable**, every write command (`SET`/`DEL`) is appended to an append-only file in RESP format; on startup the file is replayed to rebuild the dataset. This is the same mechanism Redis's AOF uses.

The **fsync policy** controls the durability/throughput trade-off:

| Policy | Behavior | Guarantee |
|--------|----------|-----------|
| `always` | `fsync()` after every write | Zero data loss, lowest throughput |
| `everysec` | Background thread `fsync()`s once/sec | ≤1s of writes lost on crash (Redis default) |
| `no` | Let the OS flush | Fastest, weakest durability |

Replay uses the existing RESP frame parser and stops cleanly at a torn final record (a partial write from a crash), so a half-written command never corrupts recovery. Writes are logged **after** they succeed, and replay runs **before** the AOF is attached, so replayed commands are never re-logged.

---

## Design Tradeoffs & Limitations

Honest engineering notes — the known limitations of the current design and how I'd address them. (Every system has a backlog; these are mine.)

| Area | Current design | Trade-off / limitation | How I'd improve it |
|------|----------------|------------------------|--------------------|
| **Read concurrency** | `get()` takes the *exclusive* lock (it updates LRU order + does lazy expiry), not the shared lock | Reads don't fully run in parallel despite the reader-writer lock | Sharded locks (lock per hash bucket) or a lock-free/approximate LRU (e.g. sampled eviction) so reads take the shared lock |
| **AOF + TTL** | A `SET key val EX 60` is logged verbatim | On replay the TTL restarts from 60s instead of counting the elapsed time | Rewrite relative expiries to absolute timestamps at log time (Redis's `PEXPIREAT` approach) |
| **AOF growth** | The log grows unbounded (every write appended) | A key written 1M times keeps 1M records | AOF rewrite/compaction: periodically snapshot current state into a fresh, minimal log |
| **Connection scaling** | Thread-per-connection via the pool | Thousands of idle connections tie up worker threads | An `epoll`/`kqueue` event loop (reactor) to handle 10K+ connections on a few threads |
| **Single node** | One process holds all data | No high availability; capacity bounded by one machine's RAM | Primary-replica replication (stream the AOF to replicas) + sharding via consistent hashing |
| **Eviction accuracy** | Strict LRU | LRU can be fooled by a full scan flushing hot keys | LRU-K or a segmented/2Q cache, or Redis-style sampled LRU approximation |

---

## Benchmarks

_Run on [your machine specs]. Results will vary by hardware._

```bash
# Start server
./build/kvstore -p 6399 -t 8

# Run benchmarks
python3 benchmarks/benchmark.py 6399
```

| Benchmark | Throughput |
|-----------|------------|
| Single-thread SET | ~16,000 ops/sec |
| Single-thread GET | ~23,600 ops/sec |
| 4-thread SET | ~57,000 ops/sec |
| 4-thread GET | ~58,000 ops/sec |
| 8-thread mixed (80% read / 20% write) | ~44,600 ops/sec |

| Latency (single-thread) | SET | GET |
|-------------------------|-----|-----|
| p50 | 52 μs | 41 μs |
| p95 | 60 μs | 48 μs |
| p99 | 66 μs | 52 μs |

_Measured on macOS, Apple Silicon. Throughput is network-bound (TCP round-trip per command); server-side processing is sub-microsecond._

### Pipelining

Batching commands per round-trip amortizes network latency — the dominant cost:

```bash
python3 benchmarks/pipeline_benchmark.py 6399
```

| Strategy | Throughput |
|----------|------------|
| Sequential (1 command / round-trip) | ~24,000 ops/sec |
| Pipelined (batch = 100) | ~96,000 ops/sec (**4.1×**) |
| Pipelined (batch = 1000) | ~110,000 ops/sec (**4.7×**) |

_The server pipelines transparently: it processes every complete command in its receive buffer before reading more._

## Testing

```bash
# Compile and run unit tests
g++ -std=c++17 -pthread -I src tests/test_parser.cpp src/parser.cpp -o build/test_parser && ./build/test_parser
g++ -std=c++17 -pthread -I src tests/test_lru.cpp src/lru_cache.cpp -o build/test_lru && ./build/test_lru
g++ -std=c++17 -pthread -I src tests/test_store.cpp src/store.cpp src/lru_cache.cpp -o build/test_store && ./build/test_store
g++ -std=c++17 -pthread -I src tests/test_thread_pool.cpp src/thread_pool.cpp -o build/test_thread_pool && ./build/test_thread_pool

# Integration tests (start server first)
./build/kvstore -p 6399 &
g++ -std=c++17 -pthread -I src tests/test_integration.cpp -o build/test_integration && ./build/test_integration 6399
```

## Project Structure

```
kvstore/
├── CMakeLists.txt              — Build configuration
├── README.md                   — This file
├── src/
│   ├── types.h                 — Common types (Entry, Command, constants)
│   ├── thread_pool.h/.cpp      — Fixed-size worker thread pool
│   ├── server.h/.cpp           — TCP server (POSIX sockets, dual-protocol framing)
│   ├── parser.h/.cpp           — Inline text command parser
│   ├── resp.h/.cpp             — RESP protocol framing/parsing + command encoding
│   ├── reply.h                 — Structured Reply → RESP or inline serialization
│   ├── lru_cache.h/.cpp        — LRU eviction (doubly-linked list + hashmap)
│   ├── store.h/.cpp            — Thread-safe KV store (shared_mutex)
│   ├── expiry_manager.h/.cpp   — Background TTL expiry (Redis-style sampling)
│   ├── persistence.h/.cpp      — AOF persistence (append, replay, fsync policy)
│   ├── handler.h/.cpp          — Command routing, execution, AOF logging
│   └── main.cpp                — Entry point, CLI args, AOF recovery, signal handling
├── tests/
│   ├── test_parser.cpp         — Parser unit tests
│   ├── test_lru.cpp            — LRU eviction unit tests
│   ├── test_store.cpp          — KV store unit tests (incl. thread safety)
│   ├── test_thread_pool.cpp    — Thread pool unit tests
│   └── test_integration.cpp    — End-to-end TCP integration tests
├── benchmarks/
│   ├── benchmark.py            — Multi-threaded throughput & latency benchmarks
│   └── pipeline_benchmark.py   — Sequential vs pipelined throughput comparison
└── docs/
    └── (design notes)
```

## CS Fundamentals Demonstrated

| Concept | Where |
|---------|-------|
| POSIX Sockets / TCP | `server.cpp` — raw socket API |
| Thread Pool / Producer-Consumer | `thread_pool.cpp` — mutex + condition_variable |
| Reader-Writer Lock | `store.cpp` — `std::shared_mutex` |
| LRU Cache (O(1)) | `lru_cache.cpp` — linked list + hash map |
| Atomic Operations | `store.cpp` — `std::atomic` for stats counters |
| Background Thread | `expiry_manager.cpp` — periodic TTL sampling |
| Write-Ahead / Append-Only Log | `persistence.cpp` — AOF durability + crash recovery |
| File I/O & fsync policy | `persistence.cpp` — POSIX `write`/`fsync`, durability tuning |
| Protocol design | `resp.cpp` + `reply.h` — RESP framing, structured replies |
| Signal Handling | `main.cpp` — SIGINT/SIGTERM graceful shutdown |
| RAII / Smart Pointers | Throughout — deterministic resource cleanup |

## Future Extensions

- [x] RESP protocol support (Redis wire-compatible)
- [x] Persistence — Append-Only File with fsync policies + crash recovery
- [x] Pipelining (batch multiple commands per round-trip)
- [ ] AOF rewrite / compaction (bound log growth)
- [ ] AOF absolute-expiry rewriting (correct TTLs across restart)
- [ ] `epoll`/`kqueue` event loop (scale to 10K+ connections)
- [ ] Pub/Sub messaging
- [ ] Primary-replica replication + cluster mode (consistent hashing)
