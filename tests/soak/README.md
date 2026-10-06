# libasyik soak tests

`libasyik_soak` runs long, randomised stability scenarios against libasyik:
fiber scheduling, `execute()`/`async()`, raw Asio I/O through
`asyik::use_fiber_future`, HTTP/HTTPS/WebSocket under normal and hostile
load, and the cache, rate limiter, object pool and SQL pool. It is meant for
local runs (it is not part of CI), from minutes to hours.

## Build

```bash
cmake -B build -DLIBASYIK_BUILD_SOAK=ON
cmake --build build --target libasyik_soak -j4
```

The executable is always built with `-O2 -g` and assertions enabled,
whatever `CMAKE_BUILD_TYPE` is, so a seed exercises the same code everywhere.
Linux only (it reads `/proc` and uses `SO_REUSEPORT`).

## Run

```bash
./build/tests/soak/libasyik_soak --list                   # scenarios
./build/tests/soak/libasyik_soak --duration=60            # all, 60s each
./build/tests/soak/libasyik_soak --scenario=http.,ws. --duration=600
./build/tests/soak/libasyik_soak --scenario=fiber.execute_storm --seed=42
```

| Option | Meaning |
|---|---|
| `--duration=S` | seconds per scenario (default 30) |
| `--seed=N` | random seed; printed at start, random by default |
| `--scenario=A,B` | run only scenarios starting with these prefixes |
| `--stall=S` | abort when a scenario makes no progress for S seconds (default 60) |
| `--port-base=N` | first TCP port (default 21100, below the ephemeral range) |

Exit status: 0 all passed, 1 a scenario failed, 2 bad arguments, 3 stall.
Every failure prints the command that reruns it with the same seed. The
seed fixes the sequence of operations each client performs; thread timing
still varies, so a rare race may need a few runs to show again.

The `sql.pool` scenario needs PostgreSQL (`ASYIK_SOAK_PG=<connect string>`,
default `host=localhost dbname=postgres password=test user=postgres`) and
is skipped without it. It only terminates connections it opened itself
(`application_name=asyik_soak`).

## What is checked

- every result: checksums of payloads, echoed bytes, HTTP bodies and
  ranges, WebSocket messages, cache values, rows in the database
- every service thread finishes within 10s of `stop()`, thread exit
  included (a fiber left behind makes the thread hang there)
- no `service::run()` had to force its exit because fibers were still
  running (counted from libasyik's own warning)
- every server-side connection or WebSocket handler fiber ends once its
  client is gone
- `async()` statistics balance (started == terminated)
- open file descriptors return to their count before the scenario
- a well-behaved HTTP client keeps getting answers within 1s while hostile
  clients misbehave

The summary also prints thread count and RSS before and after each
scenario. The first scenarios add about 80 threads (the `async()` worker
pool, hardware threads x `ASYIK_THREAD_MULTIPLIER`) and 2 x hardware
threads (Asio's `system_context`, used by `use_fiber_future`); both are
created once per process.

## Sanitizers

AddressSanitizer is not usable with a Boost that was not built for it:
Boost.Context switches stacks without telling ASan, which then crashes in
roughly 1 of 4 runs even for a plain Boost.Fiber program without libasyik.
It needs Boost built with `context-impl=ucontext` and
`define=BOOST_USE_ASAN`.

## Adding a scenario

```cpp
SOAK_SCENARIO(my_case, "area.my_case", "one-line description")
{
  soak::service_thread st;            // a service on its own thread
  while (ctx.time_left()) {
    ctx.phase("doing X");             // shown if the watchdog fires
    auto rng = ctx.rng(stream_id);    // reproducible random stream
    ...
    SOAK_CHECK(ctx, result == expected, "what went wrong");
    ctx.count("operations");          // printed in the summary
    ctx.progress();                   // feeds the watchdog
  }
  st.stop_and_join(ctx);              // fails on a shutdown hang
}
```
