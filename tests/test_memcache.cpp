#include <chrono>
#include <stdexcept>

#include "catch2/catch.hpp"
#include "libasyik/error.hpp"
#include "libasyik/memcache.hpp"
#include "libasyik/service.hpp"

namespace asyik {

void _TEST_invoke_memcache(){};

namespace {
using test_clock = std::chrono::steady_clock;

// An entry of a 1-second memcache lives at least 999ms after its last put()
// or get() (timestamps are whole milliseconds).
constexpr std::chrono::milliseconds min_lifetime_1s{990};

// Expects `lookup()` (a memcache at()/get()) to still find an entry equal to
// `expected`. `since` must be taken before the entry's last put()/get().
//
// A loaded CI machine can oversleep far beyond any margin a test leaves, so
// "still cached" cannot be assumed from the sleeps alone: a miss fails the test
// only while the minimum lifetime has not actually elapsed. Returns false when
// the entry legitimately expired, so later checks that rely on it can skip.
// (Checks that entries have expired need no such care: sleeps never return
// early.)
template <typename Lookup, typename T>
bool expect_cached(test_clock::time_point since, Lookup&& lookup,
                   const T& expected)
{
  try {
    // look up outside REQUIRE, which would swallow the out_of_range
    auto value = lookup();
    REQUIRE(value == expected);
    return true;
  } catch (std::out_of_range&) {
    REQUIRE(test_clock::now() - since >= min_lifetime_1s);
    LOG(WARNING) << "memcache entry expired because the test overslept; "
                    "check skipped\n";
    return false;
  }
}
}  // namespace

TEST_CASE("Testing basic of memcache")
{
  auto as = asyik::make_service();
  auto cache = asyik::make_memcache<std::string, int, 5>(as);

  // basic put-get
  cache->put("1", 1);
  cache->put("2", 2);
  cache->put("3", 3);

  REQUIRE(cache->at("1") == 1);
  REQUIRE(cache->at("2") == 2);
  REQUIRE(cache->at("3") == 3);

  REQUIRE(cache->get("1") == 1);
  REQUIRE(cache->get("2") == 2);
  REQUIRE(cache->get("3") == 3);

  // try out of range
  try {
    auto i = cache->at("4");

    REQUIRE(false);
  } catch (std::out_of_range& e) {
    REQUIRE(true);
  }

  try {
    auto i = cache->get("4");

    REQUIRE(false);
  } catch (std::out_of_range& e) {
    REQUIRE(true);
  }

  cache->put("3", 5);
  REQUIRE(cache->get("3") == 5);
  REQUIRE(cache->at("3") == 5);

  // try erasing an item
  cache->erase("1");
  try {
    auto i = cache->at("1");

    REQUIRE(false);
  } catch (std::out_of_range& e) {
    REQUIRE(true);
  }

  // try clearing all entries
  cache->clear();
  try {
    auto i = cache->at("2");

    REQUIRE(false);
  } catch (std::out_of_range& e) {
    REQUIRE(true);
  }

  // try creating memcache for movable only items
  auto cache2 =
      asyik::make_memcache<std::string, std::unique_ptr<std::string>, 1>(as);
  auto since = test_clock::now();
  cache2->put("1",
              std::move(std::unique_ptr<std::string>(new std::string("test"))));
  auto at_1 = [&]() {
    std::string value = *cache2->at("1");
    LOG(INFO) << "test priontout cache2:" << value << "\n";
    return value;
  };
  expect_cached(since, at_1, std::string("test"));
  asyik::sleep_for(std::chrono::milliseconds(500));
  since = test_clock::now();
  cache2->put(
      "1", std::move(std::unique_ptr<std::string>(new std::string("test2"))));
  auto get_1 = test_clock::now();
  if (expect_cached(
          since, [&]() { return std::string(*cache2->get("1")); },
          std::string("test2")))
    expect_cached(get_1, at_1, std::string("test2"));  // get() refreshed it
  asyik::sleep_for(std::chrono::milliseconds(1100));
  try  // passive pruning
  {
    auto i = cache->at("1");

    REQUIRE(false);
  } catch (std::out_of_range& e) {
    REQUIRE(true);
  }

  // cache->get("1"); //=> throw error
  // cache->at("1"); //=> throw error
  // cache->clear();
  // cache->size();
  // cache->erase();

  // cache->count("1"); //=> doesnt update

  as->stop();
  as->run();
}

TEST_CASE("Testing active pruning mechanism")
{
  auto as = asyik::make_service();
  auto cache = asyik::make_memcache<std::string, int, 1, 50>(as);

  as->execute([cache, as]() {
    // test basic timeouts (items live 1000-1020ms after their last put/get)
    auto put_1 = test_clock::now();
    cache->put("1", 11);
    cache->put("2", 22);
    cache->put("3", 3);
    asyik::sleep_for(std::chrono::milliseconds(800));

    auto get_1 = test_clock::now();
    bool has_1 = expect_cached(put_1, [&]() { return cache->get("1"); }, 11);
    auto put_3 = test_clock::now();
    cache->put("3", 33);  // update and relocate "3" to more recent partition
    asyik::sleep_for(std::chrono::milliseconds(800));

    // get() refreshed "1", so it is expected again only if it was found
    if (has_1) expect_cached(get_1, [&]() { return cache->get("1"); }, 11);
    expect_cached(put_3, [&]() { return cache->get("3"); }, 33);
    // 2 should be out of range
    try {
      auto i = cache->at("2");

      REQUIRE(false);
    } catch (std::out_of_range& e) {
      REQUIRE(true);
    }

    asyik::sleep_for(std::chrono::milliseconds(1040));

    // now "1" should be out of range too
    try {
      auto i = cache->get("1");

      REQUIRE(false);
    } catch (std::out_of_range& e) {
      REQUIRE(true);
    }

    as->stop();
  });

  as->run();
}

TEST_CASE("Testing multithreading")
{
  auto as = asyik::make_service();
  auto cache = asyik::make_memcache_mt<int, int, 1, 50>(as);

  std::atomic<int> num_done{0};
  for (int i = 0; i < 64; i++)
    as->async([cache, as, i, &num_done]() {
      // test basic timeouts (see "Testing active pruning mechanism")
      auto put_1 = test_clock::now();
      cache->put(i * 10 + 1, 11);
      cache->put(i * 10 + 2, 22);
      cache->put(i * 10 + 3, 3);
      asyik::sleep_for(std::chrono::milliseconds(800));

      auto get_1 = test_clock::now();
      bool has_1 =
          expect_cached(put_1, [&]() { return cache->get(i * 10 + 1); }, 11);
      auto put_3 = test_clock::now();
      cache->put(i * 10 + 3,
                 33);  // update and relocate "3" to more recent partition
      asyik::sleep_for(std::chrono::milliseconds(800));

      if (has_1)
        expect_cached(get_1, [&]() { return cache->get(i * 10 + 1); }, 11);
      expect_cached(put_3, [&]() { return cache->get(i * 10 + 3); }, 33);
      // 2 should be out of range
      try {
        auto k = cache->at(i * 10 + 2);

        REQUIRE(false);
      } catch (std::out_of_range& e) {
        REQUIRE(true);
      }

      // try erasing an item
      cache->erase(i * 10 + 1);
      try {
        auto k = cache->at(i * 10 + 1);

        REQUIRE(false);
      } catch (std::out_of_range& e) {
        REQUIRE(true);
      }

      num_done++;
    });

  as->execute([&num_done, as]() {
    while (num_done < 64) asyik::sleep_for(std::chrono::milliseconds(10));

    as->stop();
  });

  as->run();
}

TEST_CASE("Testing long expiry does not overflow")
{
  // 30 days: expiry * 1000 exceeds INT_MAX, which used to wrap negative and
  // expire every entry immediately
  auto as = asyik::make_service();
  auto cache = asyik::make_memcache<int, int, 30 * 24 * 3600>(as);
  auto cache_mt = asyik::make_memcache_mt<int, int, 30 * 24 * 3600>(as);

  as->execute([cache, cache_mt, as]() {
    cache->put(1, 11);
    cache_mt->put(1, 11);
    asyik::sleep_for(std::chrono::milliseconds(50));

    REQUIRE(cache->get(1) == 11);
    REQUIRE(cache->at(1) == 11);
    REQUIRE(cache_mt->get(1) == 11);
    REQUIRE(cache_mt->at(1) == 11);

    as->stop();
  });

  as->run();
}

}  // namespace asyik