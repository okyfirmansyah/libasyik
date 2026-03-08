# Benchmarking & Profiling

This page covers how to build and run the HTTP benchmark suite against libasyik and how to use the built-in pipeline profiler to identify bottlenecks during development.

---

## Table of Contents

- [Architecture of the benchmark server](#architecture-of-the-benchmark-server)
- [Building the benchmarks](#building-the-benchmarks)
- [Running the benchmark suite](#running-the-benchmark-suite)
- [Benchmark scenarios](#benchmark-scenarios)
- [Comparing against GIN and Boost.Beast](#comparing-against-gin-and-boostbeast)
- [Benchmark snapshot — 2026-03-08](#benchmark-snapshot--2026-03-08)
- [HTTP pipeline profiler](#http-pipeline-profiler)
  - [Enabling the profiler](#enabling-the-profiler)
  - [Reading the profiler output](#reading-the-profiler-output)
  - [Runtime tuning](#runtime-tuning)
- [Output files](#output-files)

---

## Architecture of the benchmark server

The benchmark server (`benchmarks/libasyik/bench_server.cpp`) implements libasyik's recommended **share-nothing, multi-acceptor** architecture:

- **N independent `asyik::service` threads** — each owns its own `io_context` and fiber scheduler.
- **N independent HTTP servers** on the same address:port using **`SO_REUSEPORT`**.  
  The Linux kernel acts as the load balancer across all acceptors.
- No shared state, no mutexes across threads.

```
  wrk ──────────────────────────────────────────────────────────
              ┌──────────────────────────────────────┐
              │         Kernel TCP stack             │
              │  SO_REUSEPORT load balancing         │
              └──┬───────────┬───────────┬───────────┘
                 │           │           │
         ┌───────▼─┐   ┌─────▼───┐  ┌───▼─────┐
         │thread 0 │   │thread 1 │  │thread N │
         │service  │   │service  │  │service  │
         │accept   │   │accept   │  │accept   │
         │handle   │   │handle   │  │handle   │
         └─────────┘   └─────────┘  └─────────┘
```

The number of threads defaults to `std::thread::hardware_concurrency()` and can be overridden with the `ASYIK_THREAD_MULTIPLIER` environment variable (or `--thread-multiplier` CLI flag).

---

## Building the benchmarks

The `setup.sh` script builds everything in one step:

```bash
bash benchmarks/setup.sh
```

This installs `wrk`, Go, fetches GIN dependencies, and compiles all three benchmark binaries:

| Binary | Location | Description |
|---|---|---|
| `bench_server` | `build_bench/benchmarks/bench_server` | libasyik server (Boost.Fiber + libasyik stack) |
| `bench_beast` | `build_bench/benchmarks/bench_beast` | Direct Boost.Beast/Asio — **theoretical ceiling** |
| `bench_gin` | `benchmarks/gin/bench_gin` | GIN (Go) reference |

To build manually:

```bash
mkdir -p build_bench && cd build_bench
cmake .. \
  -DLIBASYIK_BUILD_BENCHMARKS=ON \
  -DLIBASYIK_ENABLE_SOCI=OFF \
  -DLIBASYIK_ENABLE_SSL_SERVER=OFF
make -j$(nproc) bench_server bench_beast
```

---

## Running the benchmark suite

```bash
# Full 3-way comparison (all CPU cores, 30 s per scenario — production quality):
bash benchmarks/run_benchmark.sh \
  --target=all \
  --duration=30 \
  --concurrency=50,100,200,500 \
  --thread-multiplier=$(nproc)

# Quick sanity run (10 s, fewer concurrency levels):
bash benchmarks/run_benchmark.sh \
  --target=all \
  --duration=10 \
  --concurrency=100,200

# Single target:
bash benchmarks/run_benchmark.sh --target=beast
```

Available options:

| Flag | Default | Description |
|---|---|---|
| `--target=<libasyik\|gin\|beast\|all>` | `all` | Which server(s) to benchmark |
| `--port=<N>` | `8080` | Port for libasyik |
| `--gin-port=<N>` | `8082` | Port for GIN |
| `--beast-port=<N>` | `8086` | Port for Boost.Beast direct |
| `--duration=<N>` | `20` | Seconds per wrk run |
| `--threads=<N>` | `4` | wrk worker threads |
| `--concurrency=<a,b,c>` | `50,100,200,500` | Connection counts to sweep |
| `--delay-ms=<N>` | `5` | Simulated I/O delay for scenario D |
| `--thread-multiplier=<N>` | `4` | Server thread count (`ASYIK_THREAD_MULTIPLIER` / `BEAST_THREAD_MULTIPLIER`) |
| `--output-dir=<path>` | `results/TIMESTAMP` | Where to save raw results |

Results are saved under `benchmarks/results/<timestamp>/` and are git-ignored.

---

## Benchmark scenarios

| ID | Endpoint | What it tests |
|---|---|---|
| **A** | `GET /plaintext` | Raw throughput — tiny fixed response, no body parsing overhead |
| **B** | `GET /json` | JSON response serialization |
| **C** | `POST /echo` (small ~64 B body) | Request body read + echo back |
| **C2** | `POST /echo` (large ~4 KB body) | Large body throughput, transfer-rate bound |
| **D** | `GET /delay/<ms>` | Fiber concurrency under I/O wait (fiber-aware `sleep_for`) |

Scenario D uses a sweep of higher concurrency levels (100, 200, 500, 1000) because the interesting behaviour is how many concurrent sleeping fibers the scheduler can manage.

---

## Comparing against GIN and Boost.Beast

Three reference points are measured:

| Server | Language / Runtime | Role |
|---|---|---|
| **libasyik** | C++ / Boost.Fiber cooperative scheduler | Subject under test |
| **GIN** | Go / goroutine scheduler | Cross-language reference (closest conceptual analogue) |
| **Boost.Beast direct** | C++ / pure async callbacks, no fibers | **Theoretical ceiling** for the same Asio stack |

The Beast server uses the identical TCP setup (SO_REUSEPORT, N threads = N io_contexts) but with no libasyik layers — just raw `async_read` / `async_write` callbacks. The gap between Beast and libasyik quantifies the cost of the Boost.Fiber scheduler plus the request/response abstraction.

GIN's goroutine scheduler is highly optimised for the delay-heavy (scenario D) case, where it can efficiently park millions of goroutines. Boost.Fiber stacks use more memory per suspended fiber, which hurts at very high concurrency.

---

## Benchmark snapshot — 2026-03-08

**Configuration:**
- Hardware: 12-core x86-64, Linux (dev container)  
- Server threads: 12 (`--thread-multiplier=12`), one `io_context` + SO_REUSEPORT acceptor per thread  
- Load generator: `wrk` 4 threads, 30 s per scenario  
- GIN: `GOMAXPROCS=12` (all cores, Go default)  
- All servers on loopback (`127.0.0.1`); wrk on same machine  
- Results directory: `benchmarks/results/20260308_135313/`

### Scenario A — GET /plaintext (raw throughput)

| Concurrency | libasyik | GIN | Beast (ceiling) |
|---:|---:|---:|---:|
| 50 | **159,483** | 106,909 | 144,959 |
| 100 | **175,062** | 113,827 | 155,075 |
| 200 | **164,073** | 117,539 | 158,090 |
| 500 | **149,809** | 121,251 | 139,110 |

> libasyik leads Beast at c=50–200: the per-core fiber scheduler amortises context-switch cost better than callback chaining at moderate connection counts.

### Scenario B — GET /json

| Concurrency | libasyik | GIN | Beast (ceiling) |
|---:|---:|---:|---:|
| 50 | **118,300** | 108,855 | 140,604 |
| 100 | **128,297** | 114,451 | 153,886 |
| 200 | **135,757** | 118,642 | 157,339 |
| 500 | **133,914** | 120,799 | 148,597 |

### Scenario C — POST /echo (small ~64 B body)

| Concurrency | libasyik | GIN | Beast (ceiling) |
|---:|---:|---:|---:|
| 50 | **111,652** | 97,660 | 138,160 |
| 100 | **123,434** | 102,708 | 148,670 |
| 200 | **131,818** | 105,865 | 151,989 |
| 500 | **129,669** | 108,542 | 142,755 |

### Scenario C2 — POST /echo (large ~4 KB body)

| Concurrency | libasyik | GIN | Beast (ceiling) |
|---:|---:|---:|---:|
| 50 | **67,616** | 49,683 | 69,191 |
| 100 | **75,371** | 52,256 | 75,135 |
| 200 | **77,200** | 54,545 | 76,047 |
| 500 | **75,678** | 59,543 | 72,964 |

> libasyik matches or beats Beast on large-body echo — I/O transfer time dominates and the fiber overhead is negligible.

### Scenario D — GET /delay/5ms (I/O concurrency stress)

| Concurrency | libasyik | GIN | Beast (ceiling) |
|---:|---:|---:|---:|
| 100 | 18,665 | 16,986 | **19,044** |
| 200 | 36,721 | 33,648 | **38,648** |
| 500 | 73,498 | 78,966 | **89,190** |
| 1000 | 74,862 | 101,234 | **115,441** |

> At very high connection counts (c=1000) with suspended fibers, fiber stack memory pressure reduces throughput. Both GIN and Beast hold more connections efficiently here.

### Summary table (c=200, RPS)

| Scenario | libasyik | GIN | Beast | libasyik vs GIN | libasyik vs Beast |
|---|---:|---:|---:|---:|---:|
| A: plaintext | 164,073 | 117,539 | 158,090 | **+40%** | +4% |
| B: json | 135,757 | 118,642 | 157,339 | **+14%** | −14% |
| C: echo-small | 131,818 | 105,865 | 151,989 | **+24%** | −13% |
| C2: echo-4KB | 77,200 | 54,545 | 76,047 | **+42%** | **+2%** |
| D: delay-5ms (c=1000) | 74,862 | 101,234 | 115,441 | −26% | −35% |

### Latency (p50 / p99) at c=200 — Scenario A

| Framework | p50 | p99 |
|---|---:|---:|
| libasyik | 601 µs | 18.8 ms |
| GIN | 940 µs | 11.4 ms |
| Beast | 620 µs | 15.9 ms |

---

## HTTP pipeline profiler

### Enabling the profiler

The profiler instruments each stage of the HTTP request pipeline and prints a periodic breakdown table. It is **compiled out completely** by default (zero overhead in production).

```bash
# Build with profiling:
cd build_bench
cmake .. -DLIBASYIK_BUILD_BENCHMARKS=ON -DLIBASYIK_BENCH_PROFILING=ON
make -j$(nproc) bench_server
```

This adds `-DLIBASYIK_HTTP_PROFILING` to both the `libasyik` library and `bench_server`. The instrumentation lives in [`include/libasyik/profiling.hpp`](../include/libasyik/profiling.hpp) and is conditional on that macro.

### Reading the profiler output

The server prints a breakdown table every `ASYIK_PROF_INTERVAL` seconds (default: 5):

```
[profiling] HTTP Pipeline Breakdown  |  <N> req  <N> req/s
  Stage            |    Count |   Avg (us) |   Share
  -----------------+----------+------------+---------
  read_request     |      ... |        ... |     ...
  route_match      |      ... |        ... |     ...
  handler          |      ... |        ... |     ...
  write_response   |      ... |        ... |     ...
  -----------------+----------+------------+---------
  TOTAL            |      ... |        ... |  100.0%
```

**Stages:**

| Stage | What it covers |
|---|---|
| `read_request` | `async_read()` — TCP receive + HTTP parse of headers and body (single async call) |
| `route_match` | `find_http_route()` — regex scan through the route table |
| `handler` | User callback (your application code) |
| `write_response` | `prepare_payload()` + `async_write()` — serialization and TCP send |
| `TOTAL` | End-to-end per request (read start → write complete) |

All timings are measured from the perspective of a single fiber (wall time including fiber suspension). They represent **latency added by the framework per request**, not CPU time.

> **Note:** the `read_request` stage includes fiber suspension time while waiting for the kernel to deliver data. A high value here means the network/TCP layer is the limiting factor, not the framework code itself.

### Runtime tuning

```bash
# Set report interval (seconds):
ASYIK_PROF_INTERVAL=10 ./bench_server 8080

# Combine with thread multiplier:
ASYIK_THREAD_MULTIPLIER=12 ASYIK_PROF_INTERVAL=5 ./bench_server 8080
```

To **add the profiler to your own application** (not just bench_server):

```cpp
#include "libasyik/profiling.hpp"

// In a background fiber/thread that fires periodically:
#ifdef LIBASYIK_HTTP_PROFILING
    asyik::profiling::report(elapsed_seconds);
    asyik::profiling::reset();
#endif
```

And compile with `-DLIBASYIK_HTTP_PROFILING`.

---

## Output files

Raw wrk output and per-scenario summaries are stored under `benchmarks/results/<timestamp>/`:

```
benchmarks/results/<timestamp>/
├── libasyik_server.log          # libasyik stdout during the run
├── gin_server.log               # GIN stdout
├── beast_server.log             # Beast stdout
├── libasyik_summary.txt         # per-scenario table (libasyik)
├── gin_summary.txt              # per-scenario table (GIN)
├── beast_summary.txt            # per-scenario table (Beast)
└── libasyik_A_plaintext_c200.txt  # raw wrk output per (scenario × concurrency)
    …
```

The `results/` directory is git-ignored.
