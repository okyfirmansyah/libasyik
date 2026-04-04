#ifndef LIBASYIK_ASYIK_OBJECT_POOL_HPP
#define LIBASYIK_ASYIK_OBJECT_POOL_HPP

#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

namespace asyik {

/// A block-level free-list that pools fixed-size memory blocks.
/// Thread-safe (mutex-protected).  Held via shared_ptr so that
/// pool_allocator instances (captured inside shared_ptr control blocks)
/// keep it alive.
class block_pool {
 public:
  explicit block_pool(std::size_t block_size) : block_size_(block_size) {}

  ~block_pool() {
    for (void* p : free_list_) ::operator delete(p);
  }

  block_pool(const block_pool&) = delete;
  block_pool& operator=(const block_pool&) = delete;

  void* allocate(std::size_t bytes) {
    if (bytes <= block_size_) {
      std::lock_guard<std::mutex> lk(mu_);
      if (!free_list_.empty()) {
        void* p = free_list_.back();
        free_list_.pop_back();
        return p;
      }
    }
    return ::operator new(bytes);
  }

  void deallocate(void* p, std::size_t bytes) noexcept {
    if (bytes <= block_size_) {
      std::lock_guard<std::mutex> lk(mu_);
      free_list_.push_back(p);
    } else {
      ::operator delete(p);
    }
  }

 private:
  std::size_t block_size_;
  std::mutex mu_;
  std::vector<void*> free_list_;
};

/// STL-compatible allocator backed by a shared block_pool.
/// Used with std::allocate_shared so that the shared_ptr control block
/// AND the object are placed in a single pooled allocation.
template <typename T>
class pool_allocator {
 public:
  using value_type = T;

  explicit pool_allocator(std::shared_ptr<block_pool> pool) noexcept
      : pool_(std::move(pool)) {}

  template <typename U>
  pool_allocator(const pool_allocator<U>& other) noexcept
      : pool_(other.pool_) {}

  T* allocate(std::size_t n) {
    return static_cast<T*>(pool_->allocate(n * sizeof(T)));
  }

  void deallocate(T* p, std::size_t n) noexcept {
    pool_->deallocate(p, n * sizeof(T));
  }

  template <typename U>
  bool operator==(const pool_allocator<U>& other) const noexcept {
    return pool_ == other.pool_;
  }

  template <typename U>
  bool operator!=(const pool_allocator<U>& other) const noexcept {
    return !(*this == other);
  }

 private:
  std::shared_ptr<block_pool> pool_;

  template <typename U>
  friend class pool_allocator;
};

/// Memory pool for objects that are frequently allocated and deallocated via
/// shared_ptr.  Uses std::allocate_shared so that the shared_ptr control
/// block and the object occupy a single pooled allocation — zero heap
/// allocations on the hot path once the pool is warm.
///
/// Thread-safety: all operations are protected by a mutex inside block_pool.
template <typename T>
class shared_object_pool {
 public:
  shared_object_pool()
      // Over-allocate the block size to accommodate the shared_ptr control
      // block that std::allocate_shared places alongside T.  The exact
      // overhead is implementation-defined; 128 bytes covers all major
      // standard libraries (libstdc++ ~40B, libc++ ~48B, MSVC ~56B).
      : pool_(std::make_shared<block_pool>(sizeof(T) + 128)) {}

  shared_object_pool(const shared_object_pool&) = delete;
  shared_object_pool& operator=(const shared_object_pool&) = delete;

  /// Construct a T in pooled memory and return a shared_ptr.
  /// Both the control block and the object come from the pool.
  template <typename... Args>
  std::shared_ptr<T> acquire(Args&&... args) {
    pool_allocator<T> alloc(pool_);
    return std::allocate_shared<T>(alloc, std::forward<Args>(args)...);
  }

 private:
  std::shared_ptr<block_pool> pool_;
};

}  // namespace asyik

#endif
