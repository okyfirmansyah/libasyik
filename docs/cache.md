This is mini implementation of RAM based, internal key value store that have fixed expiration time.

Inspired from [this erlang cache library](https://github.com/fogfish/cache), the cache uses N disposable std::map(segments) under the hood. The cache applies eviction and quota policies at segment level. The oldest map table is destroyed and the new one is created when quota or TTL criteria are exceeded.

The write operation always uses youngest segment. The read operation lookup key from youngest to oldest table until it is found same time key is moved to youngest segment to prolong TTL. If none of table contains key then out-of-range exception(as with std::map) will be asserted.

The downside is inability to assign precise TTL per single cache entry. TTL is always approximated to nearest segment. (e.g. cache with 60 sec TTL and 10 segments has 6 sec accuracy on TTL).

```c++
#include "catch2/catch.hpp"
#include "libasyik/error.hpp"
#include "libasyik/service.hpp"
#include "libasyik/memcache.hpp"

auto as = asyik::make_service();
// create cache<string, int> with expiration 1s, with 50segments
auto cache = asyik::make_memcache<std::string, int, 1, 50>(as);

as->execute([cache, as]()
{
  // insert few datas
  cache->put("1", 1);
  cache->put("2", 2);
  cache->put("3", 3);
  asyik::sleep_for(std::chrono::milliseconds(960));

  REQUIRE(cache->get("1")==1); // should be there, get() will prolong the TTL
  REQUIRE(cache->at("2")==2); // should be there, but at() will not prolong the TTL
  cache->put("3", 3); // update and prolong "3" item's TTL
  asyik::sleep_for(std::chrono::milliseconds(960));

  REQUIRE(cache->get("1")==1); // "1" will stil valid, as it prolonged by get()
  REQUIRE(cache->get("3")==3); // "3" will still valid too
  
  auto i = cache->at("2"); // expired, will throw std::out_of_range exception
});

as->run();
```

#### API

| Call | Result |
|---|---|
| `put(k, v)` | stores `v` (copied or moved) with a fresh lifetime, replacing any previous value |
| `get(k)` | the value, lifetime extended; throws `std::out_of_range` when missing or expired |
| `at(k)` | like `get()` but leaves the lifetime alone |
| `try_get(k)` | `std::optional` copy of the value (lifetime extended), `std::nullopt` when missing |
| `contains(k)` | whether `k` is present and not expired (lifetime left alone) |
| `get_or_put(k, make)` | the value (lifetime extended); stores `make()` first when missing |
| `visit(k, f)` | calls `f(value&)` under the cache lock when present; returns whether it was |
| `erase(k)` | removes `k`; returns whether it was there |
| `clear()` | removes everything |
| `size()` | number of entries that have not expired |

```c++
auto cache = asyik::make_memcache<std::string, std::string, 60>(as);

if (auto v = cache->try_get("user:42"))  // no exception on a miss
  use(*v);

// load once, then serve from the cache
auto profile = cache->get_or_put("user:42", [&]() { return load_profile(42); });
```

#### Multithread-safe Cache
The example above uses **make_memcache()**, whose instance may only be used from the thread running its service (the one calling **as->run()**). Its `get()`/`at()`/`get_or_put()` return references to the stored value.

To use a cache from any thread (other services, `as->async()` workers, plain threads), create it with **make_memcache_mt()**:
```c++
auto as = asyik::make_service();
// thread-safe cache
auto cache = asyik::make_memcache_mt<std::string, int, 1, 50>(as);

as->async([cache, as]() // safe from a worker thread
{
  cache->put("1", 1);
  int one = cache->get("1");          // a copy
  cache->visit("1", [](int& v) { v++; });  // in-place change under the lock
  ...
});

as->run();
```

Every call takes the cache's internal lock (a `boost::fibers::mutex`, so a fiber waiting for it yields instead of blocking its thread). Because another thread may replace, move or expire an entry as soon as the lock is released, `get()`, `at()` and `get_or_put()` return a **copy** made under the lock instead of a reference. To change a value in place, or to read a value that cannot be copied (e.g. `std::unique_ptr`), use `visit()`, which runs the callback under the lock.

`get_or_put()` looks up and inserts in one step: when several threads ask for the same missing key, `make()` runs once and they all get its result. `make()` runs under the cache lock, so keep it short.

Entries expire, and their destructors run, on whichever thread happens to prune the cache.

### Upgrading to 1.10.0

Only code using **make_memcache_mt()** is affected:

- `get()`, `at()` (and the new `get_or_put()`) return a copy instead of a
  reference, which another thread could invalidate at any time. `auto v =
  cache->get(k)` compiles unchanged; `T& v = cache->get(k)` no longer
  compiles, and changing the value through the returned object no longer
  changes the cached one: use `visit(k, f)` for in-place changes.
- `erase()` returns whether the key was there (it returned `void`).
