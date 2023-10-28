//
// use_fiber_future.hpp
// ~~~~~~~~~~~~~~
// This source code is based and modified from Boost Asio's use_future.hpp
//
// Copyright (c) 2003-2023 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASYIK_USE_FIBER_FUTURE_HPP
#define ASYIK_USE_FIBER_FUTURE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#pragma once
#endif  // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/asio/detail/config.hpp>
#include <boost/asio/detail/push_options.hpp>
#include <boost/asio/detail/type_traits.hpp>
#include <memory>

namespace boost {
namespace asio {
namespace internal {

template <typename Function, typename Allocator>
class fiber_packaged_token;

template <typename Function, typename Allocator, typename Result>
class fiber_packaged_handler;

}  // namespace internal

/// A @ref completion_token type that causes an asynchronous operation to return
/// a future.
/**
 * The use_fiber_future_t class is a completion token type that is used to
 * indicate that an asynchronous operation should return a fibers::future
 * object. A use_fiber_future_t object may be passed as a completion token to an
 * asynchronous operation, typically using the special value @c
 * boost::asio::use_fiber_future. For example:
 *
 * @code fibers::future<std::size_t> my_future
 *   = my_socket.async_read_some(my_buffer, boost::asio::use_fiber_future);
 * @endcode
 *
 * The initiating function (async_read_some in the above example) returns a
 * future that will receive the result of the operation. If the operation
 * completes with an error_code indicating failure, it is converted into a
 * system_error and passed back to the caller via the future.
 */
template <typename Allocator = std::allocator<void> >
class use_fiber_future_t {
 public:
  /// The allocator type. The allocator is used when constructing the
  /// @c std::promise object for a given asynchronous operation.
  typedef Allocator allocator_type;

  /// Construct using default-constructed allocator.
  BOOST_ASIO_CONSTEXPR use_fiber_future_t() {}

  /// Construct using specified allocator.
  explicit use_fiber_future_t(const Allocator& allocator)
      : allocator_(allocator)
  {}

#if !defined(BOOST_ASIO_NO_DEPRECATED)
  /// (Deprecated: Use rebind().) Specify an alternate allocator.
  template <typename OtherAllocator>
  use_fiber_future_t<OtherAllocator> operator[](
      const OtherAllocator& allocator) const
  {
    return use_fiber_future_t<OtherAllocator>(allocator);
  }
#endif  // !defined(BOOST_ASIO_NO_DEPRECATED)

  /// Specify an alternate allocator.
  template <typename OtherAllocator>
  use_fiber_future_t<OtherAllocator> rebind(
      const OtherAllocator& allocator) const
  {
    return use_fiber_future_t<OtherAllocator>(allocator);
  }

  /// Obtain allocator.
  allocator_type get_allocator() const { return allocator_; }

  /// Wrap a function object in a fiber_packaged task.
  /**
   * The @c package function is used to adapt a function object as a
   * fiber_packaged task. When this adapter is passed as a completion token to
   * an asynchronous operation, the result of the function object is retuned via
   * a std::future.
   *
   * @par Example
   *
   * @code std::future<std::size_t> fut =
   *   my_socket.async_read_some(buffer,
   *     use_fiber_future([](boost::system::error_code ec, std::size_t n)
   *       {
   *         return ec ? 0 : n;
   *       }));
   * ...
   * std::size_t n = fut.get(); @endcode
   */
  template <typename Function>
  internal::fiber_packaged_token<typename decay<Function>::type, Allocator>
  operator()(BOOST_ASIO_MOVE_ARG(Function) f) const;

 private:
  // Helper type to ensure that use_fiber_future can be constexpr
  // default-constructed even when std::allocator<void> can't be.
  struct std_allocator_void {
    BOOST_ASIO_CONSTEXPR std_allocator_void() {}

    operator std::allocator<void>() const { return std::allocator<void>(); }
  };

  typename conditional<is_same<std::allocator<void>, Allocator>::value,
                       std_allocator_void, Allocator>::type allocator_;
};

/// A @ref completion_token object that causes an asynchronous operation to
/// return a future.
/**
 * See the documentation for boost::asio::use_fiber_future_t for a usage
 * example.
 */
#if defined(BOOST_ASIO_HAS_CONSTEXPR)
constexpr use_fiber_future_t<> use_fiber_future;
#elif defined(BOOST_ASIO_MSVC)
__declspec(selectany) use_fiber_future_t<> use_fiber_future;
#endif

}  // namespace asio
}  // namespace boost

#include <boost/asio/detail/pop_options.hpp>

#include "detail/use_fiber_future_impl.hpp"

namespace asyik {
constexpr boost::asio::use_fiber_future_t<> use_fiber_future;
constexpr boost::asio::use_future_t<> use_future;
}  // namespace asyik

#endif  // ASYIK_USE_FIBER_FUTURE_HPP
