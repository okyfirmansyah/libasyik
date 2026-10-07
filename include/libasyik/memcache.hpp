#ifndef LIBASYIK_MEMCACHE_HPP
#define LIBASYIK_MEMCACHE_HPP

#include <algorithm>
#include <boost/fiber/mutex.hpp>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

#include "common.hpp"
#include "libasyik/asyik_fwd.hpp"
#include "libasyik/error.hpp"
#include "libasyik/service.hpp"

namespace asyik {
void _TEST_invoke_memcache();

struct single_thread {
  using guard_type = int;
  using mutex_type = int;
  template <typename T>
  using atomic_type = T;
  // get()/at() return a reference into the cache
  template <typename T>
  using result_type = T&;
};

template <typename MutexType>
struct multi_thread {
  using guard_type = std::lock_guard<MutexType>;
  using mutex_type = MutexType;
  template <typename T>
  using atomic_type = std::atomic<T>;
  // get()/at() return a copy made under the lock: a reference would outlive
  // the lock while other threads put, erase, prune or move the entry (get()
  // moves it to a fresher segment). Use visit() for in-place access.
  template <typename T>
  using result_type = T;
};

template <class Key, class T, int expiry, int segments, typename thread_policy>
class memcache : public std::enable_shared_from_this<
                     memcache<Key, T, expiry, segments, thread_policy>> {
 public:
  struct private_ {};

 public:
  memcache() = delete;
  memcache(const memcache&) = delete;
  memcache& operator=(const memcache&) = delete;
  memcache(struct private_&&) : closed(false) {}

  ~memcache() {}

  using value_ref = typename thread_policy::template result_type<T>;

  // Stores v under k (replacing any previous value) with a fresh lifetime.
  void put(const Key& k, T v)
  {
    typename thread_policy::guard_type t(mtx);
    insert(k, std::move(v));
  }

  // Removes k; returns whether it was there.
  bool erase(const Key& k)
  {
    typename thread_policy::guard_type t(mtx);
    bool found = false;
    for (auto& m : map_list) found |= m.second.erase(k) > 0;
    return found;
  }

  void clear()
  {
    typename thread_policy::guard_type t(mtx);
    map_list.clear();
  }

  // Value of k with its lifetime extended; throws std::out_of_range when
  // missing or expired. A reference for make_memcache(), a copy for
  // make_memcache_mt() (see multi_thread).
  value_ref get(const Key& k)
  {
    typename thread_policy::guard_type t(mtx);
    if (T* v = touch(k)) return *v;
    throw std::out_of_range("item not found/out of range in memcache!");
  }

  // Like get(), but leaves the lifetime alone.
  value_ref at(const Key& k)
  {
    typename thread_policy::guard_type t(mtx);
    if (T* v = find_live(k)) return *v;
    throw std::out_of_range("item not found/out of range in memcache!");
  }

  typename thread_policy::template result_type<const T> at(const Key& k) const
  {
    return const_cast<memcache*>(this)->at(k);
  }

  // Like get() without the exception: a copy of the value (lifetime
  // extended), or nullopt when missing or expired.
  std::optional<T> try_get(const Key& k)
  {
    typename thread_policy::guard_type t(mtx);
    if (T* v = touch(k)) return *v;
    return std::nullopt;
  }

  // Whether k is present and not expired; leaves the lifetime alone.
  bool contains(const Key& k) const
  {
    auto self = const_cast<memcache*>(this);
    typename thread_policy::guard_type t(self->mtx);
    return self->find_live(k) != nullptr;
  }

  // Value of k (lifetime extended); when missing, stores make() first. The
  // lookup and the insertion are one step, so with make_memcache_mt()
  // concurrent callers never both create the value. make() runs under the
  // cache lock: keep it short.
  template <typename F>
  value_ref get_or_put(const Key& k, F&& make)
  {
    typename thread_policy::guard_type t(mtx);
    if (T* v = touch(k)) return *v;
    return insert(k, std::forward<F>(make)());
  }

  // Calls f(T&) under the cache lock if k is present and not expired (its
  // lifetime is left alone); returns whether it was. The way to modify
  // values in place, or to read non-copyable ones, in a multi-thread cache.
  template <typename F>
  bool visit(const Key& k, F&& f)
  {
    typename thread_policy::guard_type t(mtx);
    T* v = find_live(k);
    if (v) std::forward<F>(f)(*v);
    return v != nullptr;
  }

  // Number of entries that are not expired.
  size_t size() const
  {
    auto self = const_cast<memcache*>(this);
    typename thread_policy::guard_type t(self->mtx);
    int64_t current_ms = now_ms();
    size_t n = 0;
    for (auto& m : self->map_list) {
      if (current_ms > m.first) break;
      n += m.second.size();
    }
    return n;
  }

 private:
  // Entry lifetime and partition width in ms (64-bit: expiry * 1000 overflows
  // int for lifetimes above ~24.8 days)
  static constexpr int64_t expiry_ms = int64_t{expiry} * 1000;
  static constexpr int64_t segment_ms = expiry_ms / segments;

  // Milliseconds from a monotonic clock: wall-clock adjustments cannot
  // expire or extend entries, and 64 bits never wrap.
  static int64_t now_ms()
  {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
        .count();
  }

  // The helpers below expect the caller to hold mtx.

  // Segments are ordered youngest first; an entry is live while the segment
  // holding it has not expired.
  T* find_live(const Key& k)
  {
    int64_t current_ms = now_ms();
    for (auto& m : map_list) {
      if (current_ms > m.first) break;
      auto it = m.second.find(k);
      if (it != m.second.end()) return &it->second;
    }
    return nullptr;
  }

  // Segment for entries stored now: the youngest one if it still lives a
  // full lifetime, otherwise a new one.
  std::map<Key, T>& youngest_segment()
  {
    int64_t expiry_at = now_ms() + expiry_ms;
    if (map_list.empty() || map_list.begin()->first < expiry_at)
      return map_list[expiry_at + segment_ms];
    return map_list.begin()->second;
  }

  T& insert(const Key& k, T&& v)
  {
    for (auto& m : map_list) m.second.erase(k);
    return youngest_segment().emplace(k, std::move(v)).first->second;
  }

  // Live entry for k moved to the youngest segment (extending its lifetime),
  // or nullptr.
  T* touch(const Key& k)
  {
    prune();
    for (auto& m : map_list) {
      auto it = m.second.find(k);
      if (it == m.second.end()) continue;
      auto& young = youngest_segment();
      if (&young == &m.second) return &it->second;
      T& moved = young.emplace(k, std::move(it->second)).first->second;
      m.second.erase(it);
      return &moved;
    }
    return nullptr;
  }

  void prune()
  {
    int64_t current_ms = now_ms();

    for (auto iter = map_list.begin(); iter != map_list.end();) {
      if (current_ms > iter->first)
        iter = map_list.erase(iter);
      else
        ++iter;
    }
  }
  typename thread_policy::template atomic_type<bool> closed;
  std::map<int64_t, std::map<Key, T>, std::greater<int64_t>> map_list;
  typename thread_policy::mutex_type mtx;

  template <class k, class t, int e, int s>
  friend std::shared_ptr<memcache<k, t, e, s, single_thread>> make_memcache(
      service_ptr as);
  template <class k, class t, int e, int s>
  friend std::shared_ptr<
      memcache<k, t, e, s, multi_thread<boost::fibers::mutex>>>
  make_memcache_mt(service_ptr as);
};

namespace internal {
// Background pruning loop: calls step(due) until it returns false (cache
// destroyed), with due == true every expiry/segments. Sleeps in slices of at
// most 100ms because a sleeping fiber only notices service stop when its sleep
// returns; a single long sleep (3 days for a 30-day expiry) would block
// service shutdown that long.
template <int expiry, int segments, typename Step>
void prune_periodically(Step&& step)
{
  using namespace std::chrono;
  const milliseconds interval(int64_t{expiry} * 1000 / segments);
  const milliseconds slice = (std::min)(interval, milliseconds(100));
  auto next_prune = steady_clock::now() + interval;
  while (true) {
    asyik::sleep_for(slice);
    bool due = steady_clock::now() >= next_prune;
    if (due) next_prune = steady_clock::now() + interval;
    if (!step(due)) break;
  }
}
}  // namespace internal

template <class Key, class T, int expiry, int segments = 10>
inline std::shared_ptr<memcache<Key, T, expiry, segments, single_thread>>
make_memcache(service_ptr as)
{
  auto cache =
      std::make_shared<memcache<Key, T, expiry, segments, single_thread>>(
          typename memcache<Key, T, expiry, segments,
                            single_thread>::private_{});
  as->execute(
      [c = std::weak_ptr<memcache<Key, T, expiry, segments, single_thread>>(
           cache)]() {
        internal::prune_periodically<expiry, segments>([&c](bool due) {
          auto cache = c.lock();
          if (!cache) return false;
          if (due) cache->prune();
          return true;
        });
      });
  return cache;
}

template <class Key, class T, int expiry, int segments = 10>
inline std::shared_ptr<
    memcache<Key, T, expiry, segments, multi_thread<boost::fibers::mutex>>>
make_memcache_mt(service_ptr as)
{
  auto cache = std::make_shared<
      memcache<Key, T, expiry, segments, multi_thread<boost::fibers::mutex>>>(
      typename memcache<Key, T, expiry, segments,
                        multi_thread<boost::fibers::mutex>>::private_{});
  as->async([c = std::weak_ptr<memcache<Key, T, expiry, segments,
                                        multi_thread<boost::fibers::mutex>>>(
                 cache)]() {
    internal::prune_periodically<expiry, segments>([&c](bool due) {
      auto cache = c.lock();
      if (!cache) return false;
      if (due) {
        // guard is released before the (possibly last) reference to cache
        typename multi_thread<boost::fibers::mutex>::guard_type t(cache->mtx);
        cache->prune();
      }
      return true;
    });
  });
  return cache;
}
}  // namespace asyik
#endif
