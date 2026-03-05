# Benchmarking & Profiling

This page covers how to build and run the HTTP benchmark suite against libasyik and how to use the built-in pipeline profiler to identify bottlenecks during development.

---

## Table of Contents

- [Architecture of the benchmark server](#architecture-of-the-benchmark-server)
- [Building the benchmarks](#building-the-benchmarks)
- [Running the benchmark suite](#running-the-benchmark-suite)
- [Benchmark scenarios](#benchmark-scenarios)
- [Comparing against GIN (Go)](#comparing-against-gin-go)
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

```bash
# From the repo root, configure with benchmark support:
mkdir -p build_bench && cd build_bench
cmake .. -DLIBASYIK_BUILD_BENCHMARKS=ON
make -j$(nproc) bench_server

# The binary will be at:
#   build_bench/benchmarks/bench_server
```

The GIN comparison server is pre-built at `benchmarks/gin/bench_gin` (Go binary).  
To rebuild it from source:

```bash
cd benchmarks/gin
/usr/local/go/bin/go build -o bench_gin .
```

---

## Running the benchmark suite

```bash
cd benchmarks

# Quick run – compare both frameworks, 10-second runs:
pkill -9 bench_server bench_gin 2>/dev/null; sleep 1
bash run_benchmark.sh --target=all --duration=10 --concurrency=50,100,200 \
     --delay-ms=5 --thread-multiplier=12
```

Available options:

| Flag | Default | Description |
|---|---|---|
| `--target=<libasyik\|gin\|all>` | `all` | Which server(s) to benchmark |
| `--port=<N>` | `8080` | Port for libasyik |
| `--gin-port=<N>` | `8082` | Port for GIN (separate port avoids conflicts) |
| `--duration=<N>` | `20` | Seconds per wrk run |
| `--threads=<N>` | `4` | wrk worker threads |
| `--concurrency=<a,b,c>` | `50,100,200,500` | Connection counts to sweep |
| `--delay-ms=<N>` | `5` | Simulated I/O delay for scenario D |
| `--thread-multiplier=<N>` | `4` | Number of libasyik service threads |
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

## Comparing against GIN (Go)

GIN is chosen as a reference because:
- It is idiomatic, production-grade Go HTTP.
- Go's goroutine scheduler is the closest conceptual analogue to Boost.Fiber.
- Both use kernel-level TCP without userspace networking.

The gap is dominated by **fiber context-switch overhead** — each async I/O call yields and resumes the fiber.

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
├── libasyik_server.log    # server stdout during the run
├── gin_server.log         # GIN stdout
├── libasyik_summary.txt   # per-scenario table (libasyik)
├── gin_summary.txt        # per-scenario table (GIN)
└── libasyik_A_plaintext_c50.txt   # raw wrk output per run
    …
```

The `results/` directory is git-ignored.
