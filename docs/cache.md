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

#### Multithread-safe Cache
The example above use **make_memcache()** to generate cache instance that expected to only be accessed from **single thread** only, that is the same thread containing the **as->run()** main loop.

In order to create cache that is safe to be accessed in any thread, you can use **make_memcache_mt()**:
```c++
auto as = asyik::make_service();
// thread-safe cache
auto cache = asyik::make_memcache_mt<std::string, int, 1, 50>(as);

as->async([cache, as]() // it is now safe to use async(will be performed in worker thread)
{
  // insert few datas
  cache->put("1", 1);
  ...
});

as->run();
```

The cache will have internal mutex/locking to allow multi-thread access possible. Consequently, the item expiration mechanism can be performed in any thread, regardless of which thread the cache is created, or which thread that put the item. So the item's destructor can be called from anywhere.
