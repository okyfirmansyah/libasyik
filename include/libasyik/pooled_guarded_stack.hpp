#ifndef LIBASYIK_ASYIK_POOLED_GUARDED_STACK_HPP
#define LIBASYIK_ASYIK_POOLED_GUARDED_STACK_HPP

#include <sys/mman.h>

#include <boost/context/stack_context.hpp>
#include <boost/context/stack_traits.hpp>
#include <cassert>
#include <cstddef>
#include <mutex>
#include <vector>

namespace asyik {

/// A fiber stack allocator that combines pooling with mmap guard pages.
///
/// Each stack is allocated via mmap with an extra guard page at the bottom
/// (mprotect PROT_NONE). When a fiber finishes, the stack is returned to a
/// free-list instead of being munmap'd. On reuse the guard page is still in
/// place — no extra syscalls needed.
///
/// The internal storage is reference-counted (via shared_ptr), so copies of
/// the allocator share the same pool — matching the semantics of
/// boost::context::pooled_fixedsize_stack.
class pooled_guarded_stack {
 public:
  using traits_type = boost::context::stack_traits;

  explicit pooled_guarded_stack(
      std::size_t stack_size = traits_type::default_size())
      : impl_(std::make_shared<impl>(stack_size))
  {}

  boost::context::stack_context allocate() { return impl_->allocate(); }

  void deallocate(boost::context::stack_context& sctx) noexcept
  {
    impl_->deallocate(sctx);
  }

 private:
  struct impl {
    explicit impl(std::size_t stack_size) : stack_size_(stack_size)
    {
      page_size_ = traits_type::page_size();
      // Round stack_size up to a multiple of page_size
      std::size_t pages = (stack_size_ + page_size_ - 1) / page_size_;
      stack_size_ = pages * page_size_;
      // Total mmap size: stack + 1 guard page
      mmap_size_ = stack_size_ + page_size_;
    }

    ~impl()
    {
      for (void* base : free_list_) {
        ::munmap(base, mmap_size_);
      }
    }

    boost::context::stack_context allocate()
    {
      void* base = pop_or_mmap();

      boost::context::stack_context sctx;
      sctx.size = stack_size_;
      // sp points to the TOP of the usable stack (stack grows downward)
      // Layout: [guard page | usable stack]
      //         base        base+page_size  base+page_size+stack_size (= sp)
      sctx.sp = static_cast<char*>(base) + mmap_size_;
      return sctx;
    }

    void deallocate(boost::context::stack_context& sctx) noexcept
    {
      // Recover the mmap base from sp
      void* base = static_cast<char*>(sctx.sp) - mmap_size_;
      std::lock_guard<std::mutex> lk(mu_);
      free_list_.push_back(base);
    }

   private:
    void* pop_or_mmap()
    {
      {
        std::lock_guard<std::mutex> lk(mu_);
        if (!free_list_.empty()) {
          void* base = free_list_.back();
          free_list_.pop_back();
          return base;
        }
      }
      // Allocate a new stack via mmap
      void* base = ::mmap(nullptr, mmap_size_, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (base == MAP_FAILED) {
        throw std::bad_alloc();
      }
      // Set the first page as a guard page (PROT_NONE)
      if (::mprotect(base, page_size_, PROT_NONE) != 0) {
        ::munmap(base, mmap_size_);
        throw std::bad_alloc();
      }
      return base;
    }

    std::size_t stack_size_;
    std::size_t page_size_;
    std::size_t mmap_size_;
    std::mutex mu_;
    std::vector<void*> free_list_;
  };

  std::shared_ptr<impl> impl_;
};

}  // namespace asyik

#endif
