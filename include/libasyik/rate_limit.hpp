#ifndef LIBASYIK_RATE_LIMIT_HPP
#define LIBASYIK_RATE_LIMIT_HPP

#include <algorithm>
#include <boost/fiber/all.hpp>
#include <list>
#include <memory>

#include "common.hpp"
#include "libasyik/error.hpp"
#include "libasyik/memcache.hpp"
#include "libasyik/service.hpp"

namespace asyik {
void _TEST_invoke_rate_limit();

template <typename store>
class rate_limit : public std::enable_shared_from_this<rate_limit<store>> {
 public:
  rate_limit() = delete;

  unsigned int checkpoint(string_view hash, unsigned int point = 1)
  {
    store& s = static_cast<store&>(*this);

    uint32_t current_ms;
    std::lock_guard<boost::fibers::mutex> g(mtx);
    unsigned int remaining = get_remaining_int(hash, current_ms);

    // adjust remaining
    point = (point > remaining) ? remaining : point;
    remaining = remaining - point;

    // store remaining
    s.put_quota(hash, remaining, current_ms);

    return point;
  }

  void reset() { static_cast<store&>(*this).reset_store(); }

  unsigned int get_remaining(string_view hash)
  {
    uint32_t current_ms;
    std::lock_guard<boost::fibers::mutex> g(mtx);
    return get_remaining_int(hash, current_ms);
  }

 private:
  unsigned int get_remaining_int(string_view hash, uint32_t& current_ms)
  {
    store& s = static_cast<store&>(*this);

    // get interval from last checkpoit
    uint32_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

    // get remaining from store
    unsigned int remaining;
    try {
      s.get_quota(hash, remaining, current_ms);

      unsigned int distance_ms = now_ms - current_ms;

      // adjust remaining based on last checkpoint
      unsigned int recovered_quota = (distance_ms * rate) / 1000;
      if (recovered_quota) {
        remaining += recovered_quota;
        if (remaining > max_bucket) remaining = max_bucket;
        current_ms = now_ms;
      }
    } catch (std::out_of_range& e) {
      remaining = max_bucket;
      current_ms = now_ms;
    }

    return remaining;
  }

 protected:
  rate_limit(unsigned int _max_bucket, unsigned int _rate)
      : max_bucket(_max_bucket), rate(_rate)
  {
    if (_max_bucket > (_rate * 100))
      throw std::out_of_range(
          "error on rate_limit initialization, max_bucket cannot be larger "
          "than 100*rate!");
  }

  unsigned int max_bucket;
  unsigned int rate;
  boost::fibers::mutex mtx;
};

class rate_limit_memory : public rate_limit<rate_limit_memory> {
  struct private_ {};
  rate_limit_memory() = delete;

  void put_quota(string_view key, unsigned int value, uint32_t now_ms)
  {
    cache->put(std::string(key), std::make_tuple(value, now_ms));
  };
  // expecting throw if key not found
  void get_quota(string_view key, unsigned int& value, uint32_t& now_ms)
  {
    auto p = cache->get(std::string(key));
    value = std::get<0>(p);
    now_ms = std::get<1>(p);
  };

  void reset_store() { cache->clear(); };

 public:
  rate_limit_memory(struct private_&&, asyik::service_ptr as,
                    unsigned int _max_bucket, unsigned int _rate)
      : rate_limit(_max_bucket, _rate)
  {
    cache =
        asyik::make_memcache_mt<std::string, std::tuple<unsigned int, uint32_t>,
                                100, 16>(as);
  }

 private:
  decltype(make_memcache_mt<std::string, std::tuple<unsigned int, uint32_t>,
                            100, 16>(nullptr)) cache;
  friend class rate_limit<rate_limit_memory>;
  friend std::shared_ptr<rate_limit_memory> make_rate_limit_memory(
      asyik::service_ptr as, unsigned int _max_bucket, unsigned int _rate);
};

std::shared_ptr<rate_limit_memory> inline make_rate_limit_memory(
    asyik::service_ptr as, unsigned int _max_bucket, unsigned int _rate)
{
  auto limiter = std::make_shared<rate_limit_memory>(
      rate_limit_memory::private_{}, as, _max_bucket, _rate);
  return limiter;
};

}  // namespace asyik
#endif
