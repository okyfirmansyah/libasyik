//
// use_fiber_future_impl.hpp
// ~~~~~~~~~~~~~~~~~~~
// This source code is based and modified from Boost Asio's impl/use_future.hpp
//
// Copyright (c) 2003-2023 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASYIK_IMPL_USE_FIBER_FUTURE_HPP
#define ASYIK_IMPL_USE_FIBER_FUTURE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#pragma once
#endif  // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/asio/async_result.hpp>
#include <boost/asio/detail/config.hpp>
#include <boost/asio/detail/memory.hpp>
#include <boost/asio/detail/push_options.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/execution.hpp>
#include <boost/asio/packaged_task.hpp>
#include <boost/asio/system_executor.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <tuple>

namespace boost {
namespace asio {
namespace internal {

#if defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

template <typename T, typename F, typename... Args>
inline void fiber_promise_invoke_and_set(fibers::promise<T>& p, F& f,
                                         BOOST_ASIO_MOVE_ARG(Args)... args)
{
#if !defined(BOOST_ASIO_NO_EXCEPTIONS)
  try
#endif  // !defined(BOOST_ASIO_NO_EXCEPTIONS)
  {
    p.set_value(f(BOOST_ASIO_MOVE_CAST(Args)(args)...));
  }
#if !defined(BOOST_ASIO_NO_EXCEPTIONS)
  catch (...) {
    p.set_exception(std::current_exception());
  }
#endif  // !defined(BOOST_ASIO_NO_EXCEPTIONS)
}

template <typename F, typename... Args>
inline void fiber_promise_invoke_and_set(fibers::promise<void>& p, F& f,
                                         BOOST_ASIO_MOVE_ARG(Args)... args)
{
#if !defined(BOOST_ASIO_NO_EXCEPTIONS)
  try
#endif  // !defined(BOOST_ASIO_NO_EXCEPTIONS)
  {
    f(BOOST_ASIO_MOVE_CAST(Args)(args)...);
    p.set_value();
  }
#if !defined(BOOST_ASIO_NO_EXCEPTIONS)
  catch (...) {
    p.set_exception(std::current_exception());
  }
#endif  // !defined(BOOST_ASIO_NO_EXCEPTIONS)
}

#else  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

template <typename T, typename F>
inline void fiber_promise_invoke_and_set(fibers::promise<T>& p, F& f)
{
#if !defined(BOOST_ASIO_NO_EXCEPTIONS)
  try
#endif  // !defined(BOOST_ASIO_NO_EXCEPTIONS)
  {
    p.set_value(f());
  }
#if !defined(BOOST_ASIO_NO_EXCEPTIONS)
  catch (...) {
    p.set_exception(std::current_exception());
  }
#endif  // !defined(BOOST_ASIO_NO_EXCEPTIONS)
}

template <typename F, typename Args>
inline void fiber_promise_invoke_and_set(fibers::promise<void>& p, F& f)
{
#if !defined(BOOST_ASIO_NO_EXCEPTIONS)
  try
#endif  // !defined(BOOST_ASIO_NO_EXCEPTIONS)
  {
    f();
    p.set_value();
#if !defined(BOOST_ASIO_NO_EXCEPTIONS)
  } catch (...) {
    p.set_exception(std::current_exception());
  }
#endif  // !defined(BOOST_ASIO_NO_EXCEPTIONS)
}

#if defined(BOOST_ASIO_NO_EXCEPTIONS)

#define BOOST_ASIO_PRIVATE_PROMISE_INVOKE_DEF(n)                               \
  template <typename T, typename F, BOOST_ASIO_VARIADIC_TPARAMS(n)>            \
  inline void fiber_promise_invoke_and_set(fibers::promise<T>& p, F& f,        \
                                           BOOST_ASIO_VARIADIC_MOVE_PARAMS(n)) \
  {                                                                            \
    p.set_value(f(BOOST_ASIO_VARIADIC_MOVE_ARGS(n)));                          \
  }                                                                            \
                                                                               \
  template <typename F, BOOST_ASIO_VARIADIC_TPARAMS(n)>                        \
  inline void fiber_promise_invoke_and_set(fibers::promise<void>& p, F& f,     \
                                           BOOST_ASIO_VARIADIC_MOVE_PARAMS(n)) \
  {                                                                            \
    f(BOOST_ASIO_VARIADIC_MOVE_ARGS(n));                                       \
    p.set_value();                                                             \
  }                                                                            \
  /**/
BOOST_ASIO_VARIADIC_GENERATE(BOOST_ASIO_PRIVATE_PROMISE_INVOKE_DEF)
#undef BOOST_ASIO_PRIVATE_PROMISE_INVOKE_DEF

#else  // defined(BOOST_ASIO_NO_EXCEPTIONS)

#define BOOST_ASIO_PRIVATE_PROMISE_INVOKE_DEF(n)                               \
  template <typename T, typename F, BOOST_ASIO_VARIADIC_TPARAMS(n)>            \
  inline void fiber_promise_invoke_and_set(fibers::promise<T>& p, F& f,        \
                                           BOOST_ASIO_VARIADIC_MOVE_PARAMS(n)) \
  {                                                                            \
    try {                                                                      \
      p.set_value(f(BOOST_ASIO_VARIADIC_MOVE_ARGS(n)));                        \
    } catch (...) {                                                            \
      p.set_exception(std::current_exception());                               \
    }                                                                          \
  }                                                                            \
                                                                               \
  template <typename F, BOOST_ASIO_VARIADIC_TPARAMS(n)>                        \
  inline void fiber_promise_invoke_and_set(fibers::promise<void>& p, F& f,     \
                                           BOOST_ASIO_VARIADIC_MOVE_PARAMS(n)) \
  {                                                                            \
    try {                                                                      \
      f(BOOST_ASIO_VARIADIC_MOVE_ARGS(n));                                     \
      p.set_value();                                                           \
    } catch (...) {                                                            \
      p.set_exception(std::current_exception());                               \
    }                                                                          \
  }                                                                            \
  /**/
  BOOST_ASIO_VARIADIC_GENERATE(BOOST_ASIO_PRIVATE_PROMISE_INVOKE_DEF)
#undef BOOST_ASIO_PRIVATE_PROMISE_INVOKE_DEF

#endif  // defined(BOOST_ASIO_NO_EXCEPTIONS)

#endif  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

// A function object adapter to invoke a nullary function object and capture
// any exception thrown into a promise.
template <typename T, typename F>
class fiber_promise_invoker {
 public:
  fiber_promise_invoker(const std::shared_ptr<fibers::promise<T>>& p,
                        BOOST_ASIO_MOVE_ARG(F) f)
      : p_(p), f_(BOOST_ASIO_MOVE_CAST(F)(f))
  {}

  void operator()()
  {
#if !defined(BOOST_ASIO_NO_EXCEPTIONS)
    try
#endif  // !defined(BOOST_ASIO_NO_EXCEPTIONS)
    {
      f_();
    }
#if !defined(BOOST_ASIO_NO_EXCEPTIONS)
    catch (...) {
      p_->set_exception(std::current_exception());
    }
#endif  // !defined(BOOST_ASIO_NO_EXCEPTIONS)
  }

 private:
  std::shared_ptr<fibers::promise<T>> p_;
  typename decay<F>::type f_;
};

// An executor that adapts the system_executor to capture any exeption thrown
// by a submitted function object and save it into a promise.
template <typename T, typename Blocking = execution::blocking_t::possibly_t>
class fiber_promise_executor {
 public:
  explicit fiber_promise_executor(const std::shared_ptr<fibers::promise<T>>& p)
      : p_(p)
  {}

  execution_context& query(execution::context_t) const BOOST_ASIO_NOEXCEPT
  {
    return boost::asio::query(system_executor(), execution::context);
  }

  static BOOST_ASIO_CONSTEXPR Blocking query(execution::blocking_t)
  {
    return Blocking();
  }

  fiber_promise_executor<T, execution::blocking_t::possibly_t> require(
      execution::blocking_t::possibly_t) const
  {
    return fiber_promise_executor<T, execution::blocking_t::possibly_t>(p_);
  }

  fiber_promise_executor<T, execution::blocking_t::never_t> require(
      execution::blocking_t::never_t) const
  {
    return fiber_promise_executor<T, execution::blocking_t::never_t>(p_);
  }

  template <typename F>
  void execute(BOOST_ASIO_MOVE_ARG(F) f) const
  {
#if defined(BOOST_ASIO_NO_DEPRECATED)
    boost::asio::require(system_executor(), Blocking())
        .execute(fiber_promise_invoker<T, F>(p_, BOOST_ASIO_MOVE_CAST(F)(f)));
#else   // defined(BOOST_ASIO_NO_DEPRECATED)
    execution::execute(
        boost::asio::require(system_executor(), Blocking()),
        fiber_promise_invoker<T, F>(p_, BOOST_ASIO_MOVE_CAST(F)(f)));
#endif  // defined(BOOST_ASIO_NO_DEPRECATED)
  }

#if !defined(BOOST_ASIO_NO_TS_EXECUTORS)
  execution_context& context() const BOOST_ASIO_NOEXCEPT
  {
    return system_executor().context();
  }

  void on_work_started() const BOOST_ASIO_NOEXCEPT {}
  void on_work_finished() const BOOST_ASIO_NOEXCEPT {}

  template <typename F, typename A>
  void dispatch(BOOST_ASIO_MOVE_ARG(F) f, const A&) const
  {
    fiber_promise_invoker<T, F>(p_, BOOST_ASIO_MOVE_CAST(F)(f))();
  }

  template <typename F, typename A>
  void post(BOOST_ASIO_MOVE_ARG(F) f, const A& a) const
  {
    system_executor().post(
        fiber_promise_invoker<T, F>(p_, BOOST_ASIO_MOVE_CAST(F)(f)), a);
  }

  template <typename F, typename A>
  void defer(BOOST_ASIO_MOVE_ARG(F) f, const A& a) const
  {
    system_executor().defer(
        fiber_promise_invoker<T, F>(p_, BOOST_ASIO_MOVE_CAST(F)(f)), a);
  }
#endif  // !defined(BOOST_ASIO_NO_TS_EXECUTORS)

  friend bool operator==(const fiber_promise_executor& a,
                         const fiber_promise_executor& b) BOOST_ASIO_NOEXCEPT
  {
    return a.p_ == b.p_;
  }

  friend bool operator!=(const fiber_promise_executor& a,
                         const fiber_promise_executor& b) BOOST_ASIO_NOEXCEPT
  {
    return a.p_ != b.p_;
  }

 private:
  std::shared_ptr<fibers::promise<T>> p_;
};

// The base class for all completion handlers that create promises.
template <typename T>
class fiber_promise_creator {
 public:
  typedef fiber_promise_executor<T> executor_type;

  executor_type get_executor() const BOOST_ASIO_NOEXCEPT
  {
    return executor_type(p_);
  }

  typedef fibers::future<T> future_type;

  future_type get_future() { return p_->get_future(); }

 protected:
  template <typename Allocator>
  void create_promise(const Allocator& a)
  {
    BOOST_ASIO_REBIND_ALLOC(Allocator, char) b(a);
    p_ = std::allocate_shared<fibers::promise<T>>(b, std::allocator_arg, b);
  }

  std::shared_ptr<fibers::promise<T>> p_;
};

// For completion signature void().
class fiber_promise_handler_0 : public fiber_promise_creator<void> {
 public:
  void operator()() { this->p_->set_value(); }
};

template <typename P>
void asyik_set_error(const boost::system::error_code& ec, P& p_)
{
  if ((ec == asio::error::timed_out) ||
      (ec == asio::error::operation_aborted) || (ec == beast::error::timeout))
    p_->set_exception(std::make_exception_ptr(
        asyik::network_timeout_error(ec,
                                     "[asyik::network_timeout_error]timeout "
                                     "error during http::async_read")));
  else if (ec == beast::http::error::header_limit) {
    p_->set_exception(std::make_exception_ptr(asyik::overflow_error(
        ec,
        "[asyik::overflow_error]incoming request header size is too large")));
  } else if ((ec == beast::http::error::body_limit) ||
             (ec == beast::websocket::error::buffer_overflow) ||
             (ec == beast::websocket::error::message_too_big)) {
    p_->set_exception(std::make_exception_ptr(asyik::overflow_error(
        ec, "[asyik::overflow_error]incoming request body size is too large")));
  } else if ((ec == beast::http::error::end_of_stream) ||
             (ec == asio::ssl::error::stream_truncated) ||
             (ec == asio::error::connection_reset) ||
             (ec == beast::websocket::error::closed) ||
             (ec == asio::error::eof)) {
    p_->set_exception(std::make_exception_ptr(
        asyik::already_closed_error(ec,
                                    "[asyik::already_closed_error]end of "
                                    "stream or network connection closed")));
  } else
    p_->set_exception(
        std::make_exception_ptr(boost::system::system_error(ec, ec.message())));
}

// For completion signature void(error_code).
class fiber_promise_handler_ec_0 : public fiber_promise_creator<void> {
 public:
  void operator()(const boost::system::error_code& ec)
  {
    if (ec) {
      asyik_set_error(ec, this->p_);
    } else {
      this->p_->set_value();
    }
  }
};

// For completion signature void(exception_ptr).
class fiber_promise_handler_ex_0 : public fiber_promise_creator<void> {
 public:
  void operator()(const std::exception_ptr& ex)
  {
    if (ex) {
      this->p_->set_exception(ex);
    } else {
      this->p_->set_value();
    }
  }
};

// For completion signature void(T).
template <typename T>
class fiber_promise_handler_1 : public fiber_promise_creator<T> {
 public:
  template <typename Arg>
  void operator()(BOOST_ASIO_MOVE_ARG(Arg) arg)
  {
    this->p_->set_value(BOOST_ASIO_MOVE_CAST(Arg)(arg));
  }
};

// For completion signature void(error_code, T).
template <typename T>
class fiber_promise_handler_ec_1 : public fiber_promise_creator<T> {
 public:
  template <typename Arg>
  void operator()(const boost::system::error_code& ec,
                  BOOST_ASIO_MOVE_ARG(Arg) arg)
  {
    if (ec)
      asyik_set_error(ec, this->p_);
    else
      this->p_->set_value(BOOST_ASIO_MOVE_CAST(Arg)(arg));
  }
};

// For completion signature void(exception_ptr, T).
template <typename T>
class fiber_promise_handler_ex_1 : public fiber_promise_creator<T> {
 public:
  template <typename Arg>
  void operator()(const std::exception_ptr& ex, BOOST_ASIO_MOVE_ARG(Arg) arg)
  {
    if (ex)
      this->p_->set_exception(ex);
    else
      this->p_->set_value(BOOST_ASIO_MOVE_CAST(Arg)(arg));
  }
};

// For completion signature void(T1, ..., Tn);
template <typename T>
class fiber_promise_handler_n : public fiber_promise_creator<T> {
 public:
#if defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

  template <typename... Args>
  void operator()(BOOST_ASIO_MOVE_ARG(Args)... args)
  {
    this->p_->set_value(
        std::forward_as_tuple(BOOST_ASIO_MOVE_CAST(Args)(args)...));
  }

#else  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

#define BOOST_ASIO_PRIVATE_CALL_OP_DEF(n)                         \
  template <BOOST_ASIO_VARIADIC_TPARAMS(n)>                       \
  void operator()(BOOST_ASIO_VARIADIC_MOVE_PARAMS(n))             \
  {                                                               \
    this->p_->set_value(                                          \
        std::forward_as_tuple(BOOST_ASIO_VARIADIC_MOVE_ARGS(n))); \
  }                                                               \
  /**/
  BOOST_ASIO_VARIADIC_GENERATE(BOOST_ASIO_PRIVATE_CALL_OP_DEF)
#undef BOOST_ASIO_PRIVATE_CALL_OP_DEF

#endif  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)
};

// For completion signature void(error_code, T1, ..., Tn);
template <typename T>
class fiber_promise_handler_ec_n : public fiber_promise_creator<T> {
 public:
#if defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

  template <typename... Args>
  void operator()(const boost::system::error_code& ec,
                  BOOST_ASIO_MOVE_ARG(Args)... args)
  {
    if (ec) {
      asyik_set_error(ec, this->p_);
    } else {
      this->p_->set_value(
          std::forward_as_tuple(BOOST_ASIO_MOVE_CAST(Args)(args)...));
    }
  }

#else  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

#define BOOST_ASIO_PRIVATE_CALL_OP_DEF(n)                           \
  template <BOOST_ASIO_VARIADIC_TPARAMS(n)>                         \
  void operator()(const boost::system::error_code& ec,              \
                  BOOST_ASIO_VARIADIC_MOVE_PARAMS(n))               \
  {                                                                 \
    if (ec) {                                                       \
      asyik_set_error(ec, this->p_);                                \
    } else {                                                        \
      this->p_->set_value(                                          \
          std::forward_as_tuple(BOOST_ASIO_VARIADIC_MOVE_ARGS(n))); \
    }                                                               \
  }                                                                 \
  /**/
  BOOST_ASIO_VARIADIC_GENERATE(BOOST_ASIO_PRIVATE_CALL_OP_DEF)
#undef BOOST_ASIO_PRIVATE_CALL_OP_DEF

#endif  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)
};

// For completion signature void(exception_ptr, T1, ..., Tn);
template <typename T>
class fiber_promise_handler_ex_n : public fiber_promise_creator<T> {
 public:
#if defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

  template <typename... Args>
  void operator()(const std::exception_ptr& ex,
                  BOOST_ASIO_MOVE_ARG(Args)... args)
  {
    if (ex)
      this->p_->set_exception(ex);
    else {
      this->p_->set_value(
          std::forward_as_tuple(BOOST_ASIO_MOVE_CAST(Args)(args)...));
    }
  }

#else  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

#define BOOST_ASIO_PRIVATE_CALL_OP_DEF(n)                           \
  template <BOOST_ASIO_VARIADIC_TPARAMS(n)>                         \
  void operator()(const std::exception_ptr& ex,                     \
                  BOOST_ASIO_VARIADIC_MOVE_PARAMS(n))               \
  {                                                                 \
    if (ex)                                                         \
      this->p_->set_exception(ex);                                  \
    else {                                                          \
      this->p_->set_value(                                          \
          std::forward_as_tuple(BOOST_ASIO_VARIADIC_MOVE_ARGS(n))); \
    }                                                               \
  }                                                                 \
  /**/
  BOOST_ASIO_VARIADIC_GENERATE(BOOST_ASIO_PRIVATE_CALL_OP_DEF)
#undef BOOST_ASIO_PRIVATE_CALL_OP_DEF

#endif  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)
};

// Helper template to choose the appropriate concrete promise handler
// implementation based on the supplied completion signature.
template <typename>
class fiber_promise_handler_selector;

template <>
class fiber_promise_handler_selector<void()> : public fiber_promise_handler_0 {
};

template <>
class fiber_promise_handler_selector<void(boost::system::error_code)>
    : public fiber_promise_handler_ec_0 {};

template <>
class fiber_promise_handler_selector<void(std::exception_ptr)>
    : public fiber_promise_handler_ex_0 {};

template <typename Arg>
class fiber_promise_handler_selector<void(Arg)>
    : public fiber_promise_handler_1<Arg> {};

template <typename Arg>
class fiber_promise_handler_selector<void(boost::system::error_code, Arg)>
    : public fiber_promise_handler_ec_1<Arg> {};

template <typename Arg>
class fiber_promise_handler_selector<void(std::exception_ptr, Arg)>
    : public fiber_promise_handler_ex_1<Arg> {};

#if defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

template <typename... Arg>
class fiber_promise_handler_selector<void(Arg...)>
    : public fiber_promise_handler_n<std::tuple<Arg...>> {};

template <typename... Arg>
class fiber_promise_handler_selector<void(boost::system::error_code, Arg...)>
    : public fiber_promise_handler_ec_n<std::tuple<Arg...>> {};

template <typename... Arg>
class fiber_promise_handler_selector<void(std::exception_ptr, Arg...)>
    : public fiber_promise_handler_ex_n<std::tuple<Arg...>> {};

#else  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

#define BOOST_ASIO_PRIVATE_PROMISE_SELECTOR_DEF(n)                          \
  template <typename Arg, BOOST_ASIO_VARIADIC_TPARAMS(n)>                   \
  class fiber_promise_handler_selector<void(Arg,                            \
                                            BOOST_ASIO_VARIADIC_TARGS(n))>  \
      : public fiber_promise_handler_n<                                     \
            std::tuple<Arg, BOOST_ASIO_VARIADIC_TARGS(n)>> {};              \
                                                                            \
  template <typename Arg, BOOST_ASIO_VARIADIC_TPARAMS(n)>                   \
  class fiber_promise_handler_selector<void(boost::system::error_code, Arg, \
                                            BOOST_ASIO_VARIADIC_TARGS(n))>  \
      : public fiber_promise_handler_ec_n<                                  \
            std::tuple<Arg, BOOST_ASIO_VARIADIC_TARGS(n)>> {};              \
                                                                            \
  template <typename Arg, BOOST_ASIO_VARIADIC_TPARAMS(n)>                   \
  class fiber_promise_handler_selector<void(std::exception_ptr, Arg,        \
                                            BOOST_ASIO_VARIADIC_TARGS(n))>  \
      : public fiber_promise_handler_ex_n<                                  \
            std::tuple<Arg, BOOST_ASIO_VARIADIC_TARGS(n)>> {};              \
  /**/
BOOST_ASIO_VARIADIC_GENERATE_5(BOOST_ASIO_PRIVATE_PROMISE_SELECTOR_DEF)
#undef BOOST_ASIO_PRIVATE_PROMISE_SELECTOR_DEF

#endif  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

// Completion handlers produced from the use_fiber_future completion token, when
// not using use_fiber_future::operator().
template <typename Signature, typename Allocator>
class fiber_promise_handler : public fiber_promise_handler_selector<Signature> {
 public:
  typedef Allocator allocator_type;
  typedef void result_type;

  fiber_promise_handler(use_fiber_future_t<Allocator> u)
      : allocator_(u.get_allocator())
  {
    this->create_promise(allocator_);
  }

  allocator_type get_allocator() const BOOST_ASIO_NOEXCEPT
  {
    return allocator_;
  }

 private:
  Allocator allocator_;
};

template <typename Function>
struct fiber_promise_function_wrapper {
  explicit fiber_promise_function_wrapper(Function& f)
      : function_(BOOST_ASIO_MOVE_CAST(Function)(f))
  {}

  explicit fiber_promise_function_wrapper(const Function& f) : function_(f) {}

  void operator()() { function_(); }

  Function function_;
};

#if !defined(BOOST_ASIO_NO_DEPRECATED)

template <typename Function, typename Signature, typename Allocator>
inline void asio_handler_invoke(Function& f,
                                fiber_promise_handler<Signature, Allocator>* h)
{
  typename fiber_promise_handler<Signature, Allocator>::executor_type ex(
      h->get_executor());
  boost::asio::dispatch(ex, fiber_promise_function_wrapper<Function>(f));
}

template <typename Function, typename Signature, typename Allocator>
inline void asio_handler_invoke(const Function& f,
                                fiber_promise_handler<Signature, Allocator>* h)
{
  typename fiber_promise_handler<Signature, Allocator>::executor_type ex(
      h->get_executor());
  boost::asio::dispatch(ex, fiber_promise_function_wrapper<Function>(f));
}

#endif  // !defined(BOOST_ASIO_NO_DEPRECATED)

// Helper base class for async_result specialisation.
template <typename Signature, typename Allocator>
class fiber_promise_async_result {
 public:
  typedef fiber_promise_handler<Signature, Allocator> completion_handler_type;
  typedef typename completion_handler_type::future_type return_type;

  explicit fiber_promise_async_result(completion_handler_type& h)
      : future_(h.get_future())
  {}

  return_type get() { return BOOST_ASIO_MOVE_CAST(return_type)(future_); }

 private:
  return_type future_;
};

// Return value from use_fiber_future::operator().
template <typename Function, typename Allocator>
class fiber_packaged_token {
 public:
  fiber_packaged_token(Function f, const Allocator& a)
      : function_(BOOST_ASIO_MOVE_CAST(Function)(f)), allocator_(a)
  {}

  // private:
  Function function_;
  Allocator allocator_;
};

// Completion handlers produced from the use_fiber_future completion token, when
// using use_fiber_future::operator().
template <typename Function, typename Allocator, typename Result>
class fiber_packaged_handler : public fiber_promise_creator<Result> {
 public:
  typedef Allocator allocator_type;
  typedef void result_type;

  fiber_packaged_handler(fiber_packaged_token<Function, Allocator> t)
      : function_(BOOST_ASIO_MOVE_CAST(Function)(t.function_)),
        allocator_(t.allocator_)
  {
    this->create_promise(allocator_);
  }

  allocator_type get_allocator() const BOOST_ASIO_NOEXCEPT
  {
    return allocator_;
  }

#if defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

  template <typename... Args>
  void operator()(BOOST_ASIO_MOVE_ARG(Args)... args)
  {
    (fiber_promise_invoke_and_set)(*this->p_, function_,
                                   BOOST_ASIO_MOVE_CAST(Args)(args)...);
  }

#else  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

  void operator()() { (fiber_promise_invoke_and_set)(*this->p_, function_); }

#define BOOST_ASIO_PRIVATE_CALL_OP_DEF(n)                             \
  template <BOOST_ASIO_VARIADIC_TPARAMS(n)>                           \
  void operator()(BOOST_ASIO_VARIADIC_MOVE_PARAMS(n))                 \
  {                                                                   \
    (fiber_promise_invoke_and_set)(*this->p_, function_,              \
                                   BOOST_ASIO_VARIADIC_MOVE_ARGS(n)); \
  }                                                                   \
  /**/
  BOOST_ASIO_VARIADIC_GENERATE(BOOST_ASIO_PRIVATE_CALL_OP_DEF)
#undef BOOST_ASIO_PRIVATE_CALL_OP_DEF

#endif  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

 private:
  Function function_;
  Allocator allocator_;
};

#if !defined(BOOST_ASIO_NO_DEPRECATED)

template <typename Function, typename Function1, typename Allocator,
          typename Result>
inline void asio_handler_invoke(
    Function& f, fiber_packaged_handler<Function1, Allocator, Result>* h)
{
  typename fiber_packaged_handler<Function1, Allocator, Result>::executor_type
      ex(h->get_executor());
  boost::asio::dispatch(ex, fiber_promise_function_wrapper<Function>(f));
}

template <typename Function, typename Function1, typename Allocator,
          typename Result>
inline void asio_handler_invoke(
    const Function& f, fiber_packaged_handler<Function1, Allocator, Result>* h)
{
  typename fiber_packaged_handler<Function1, Allocator, Result>::executor_type
      ex(h->get_executor());
  boost::asio::dispatch(ex, fiber_promise_function_wrapper<Function>(f));
}

#endif  // !defined(BOOST_ASIO_NO_DEPRECATED)

// Helper base class for async_result specialisation.
template <typename Function, typename Allocator, typename Result>
class fiber_packaged_async_result {
 public:
  typedef fiber_packaged_handler<Function, Allocator, Result>
      completion_handler_type;
  typedef typename completion_handler_type::future_type return_type;

  explicit fiber_packaged_async_result(completion_handler_type& h)
      : future_(h.get_future())
  {}

  return_type get() { return BOOST_ASIO_MOVE_CAST(return_type)(future_); }

 private:
  return_type future_;
};

}  // namespace internal

template <typename Allocator>
template <typename Function>
inline internal::fiber_packaged_token<typename decay<Function>::type, Allocator>
    use_fiber_future_t<Allocator>::operator()(BOOST_ASIO_MOVE_ARG(Function)
                                                  f) const
{
  return internal::fiber_packaged_token<typename decay<Function>::type,
                                        Allocator>(
      BOOST_ASIO_MOVE_CAST(Function)(f), allocator_);
}

#if !defined(GENERATING_DOCUMENTATION)

#if defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

template <typename Allocator, typename Result, typename... Args>
class async_result<use_fiber_future_t<Allocator>, Result(Args...)>
    : public internal::fiber_promise_async_result<
          void(typename decay<Args>::type...), Allocator> {
 public:
  explicit async_result(typename internal::fiber_promise_async_result<
                        void(typename decay<Args>::type...),
                        Allocator>::completion_handler_type& h)
      : internal::fiber_promise_async_result<
            void(typename decay<Args>::type...), Allocator>(h)
  {}
};

template <typename Function, typename Allocator, typename Result,
          typename... Args>
class async_result<internal::fiber_packaged_token<Function, Allocator>,
                   Result(Args...)>
    : public internal::fiber_packaged_async_result<
          Function, Allocator, typename result_of<Function(Args...)>::type> {
 public:
  explicit async_result(
      typename internal::fiber_packaged_async_result<
          Function, Allocator,
          typename result_of<Function(Args...)>::type>::completion_handler_type&
          h)
      : internal::fiber_packaged_async_result<
            Function, Allocator, typename result_of<Function(Args...)>::type>(h)
  {}
};

#else  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

template <typename Allocator, typename Result>
class async_result<use_fiber_future_t<Allocator>, Result()>
    : public internal::fiber_promise_async_result<void(), Allocator> {
 public:
  explicit async_result(typename internal::fiber_promise_async_result<
                        void(), Allocator>::completion_handler_type& h)
      : internal::fiber_promise_async_result<void(), Allocator>(h)
  {}
};

template <typename Function, typename Allocator, typename Result>
class async_result<internal::fiber_packaged_token<Function, Allocator>,
                   Result()>
    : public internal::fiber_packaged_async_result<
          Function, Allocator, typename result_of<Function()>::type> {
 public:
  explicit async_result(
      typename internal::fiber_packaged_async_result<
          Function, Allocator,
          typename result_of<Function()>::type>::completion_handler_type& h)
      : internal::fiber_packaged_async_result<
            Function, Allocator, typename result_of<Function()>::type>(h)
  {}
};

#define BOOST_ASIO_PRIVATE_ASYNC_RESULT_DEF(n)                            \
  template <typename Allocator, typename Result,                          \
            BOOST_ASIO_VARIADIC_TPARAMS(n)>                               \
  class async_result<use_fiber_future_t<Allocator>,                       \
                     Result(BOOST_ASIO_VARIADIC_TARGS(n))>                \
      : public internal::fiber_promise_async_result<                      \
            void(BOOST_ASIO_VARIADIC_DECAY(n)), Allocator> {              \
   public:                                                                \
    explicit async_result(typename internal::fiber_promise_async_result<  \
                          void(BOOST_ASIO_VARIADIC_DECAY(n)),             \
                          Allocator>::completion_handler_type& h)         \
        : internal::fiber_promise_async_result<                           \
              void(BOOST_ASIO_VARIADIC_DECAY(n)), Allocator>(h)           \
    {}                                                                    \
  };                                                                      \
                                                                          \
  template <typename Function, typename Allocator, typename Result,       \
            BOOST_ASIO_VARIADIC_TPARAMS(n)>                               \
  class async_result<internal::fiber_packaged_token<Function, Allocator>, \
                     Result(BOOST_ASIO_VARIADIC_TARGS(n))>                \
      : public internal::fiber_packaged_async_result<                     \
            Function, Allocator,                                          \
            typename result_of<Function(                                  \
                BOOST_ASIO_VARIADIC_TARGS(n))>::type> {                   \
   public:                                                                \
    explicit async_result(                                                \
        typename internal::fiber_packaged_async_result<                   \
            Function, Allocator,                                          \
            typename result_of<Function(BOOST_ASIO_VARIADIC_TARGS(        \
                n))>::type>::completion_handler_type& h)                  \
        : internal::fiber_packaged_async_result<                          \
              Function, Allocator,                                        \
              typename result_of<Function(                                \
                  BOOST_ASIO_VARIADIC_TARGS(n))>::type>(h)                \
    {}                                                                    \
  };                                                                      \
  /**/
BOOST_ASIO_VARIADIC_GENERATE(BOOST_ASIO_PRIVATE_ASYNC_RESULT_DEF)
#undef BOOST_ASIO_PRIVATE_ASYNC_RESULT_DEF

#endif  // defined(BOOST_ASIO_HAS_VARIADIC_TEMPLATES)

namespace traits {

#if !defined(BOOST_ASIO_HAS_DEDUCED_EQUALITY_COMPARABLE_TRAIT)

template <typename T, typename Blocking>
struct equality_comparable<
    boost::asio::internal::fiber_promise_executor<T, Blocking>> {
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_valid = true);
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_noexcept = true);
};

#endif  // !defined(BOOST_ASIO_HAS_DEDUCED_EQUALITY_COMPARABLE_TRAIT)

#if !defined(BOOST_ASIO_HAS_DEDUCED_EXECUTE_MEMBER_TRAIT)

template <typename T, typename Blocking, typename Function>
struct execute_member<
    boost::asio::internal::fiber_promise_executor<T, Blocking>, Function> {
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_valid = true);
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_noexcept = false);
  typedef void result_type;
};

#endif  // !defined(BOOST_ASIO_HAS_DEDUCED_EXECUTE_MEMBER_TRAIT)

#if !defined(BOOST_ASIO_HAS_DEDUCED_QUERY_STATIC_CONSTEXPR_TRAIT)

template <typename T, typename Blocking, typename Property>
struct query_static_constexpr_member<
    boost::asio::internal::fiber_promise_executor<T, Blocking>, Property,
    typename boost::asio::enable_if<boost::asio::is_convertible<
        Property, boost::asio::execution::blocking_t>::value>::type> {
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_valid = true);
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_noexcept = true);
  typedef Blocking result_type;

  static BOOST_ASIO_CONSTEXPR result_type value() BOOST_ASIO_NOEXCEPT
  {
    return Blocking();
  }
};

#endif  // !defined(BOOST_ASIO_HAS_DEDUCED_QUERY_STATIC_CONSTEXPR_TRAIT)

#if !defined(BOOST_ASIO_HAS_DEDUCED_QUERY_MEMBER_TRAIT)

template <typename T, typename Blocking>
struct query_member<boost::asio::internal::fiber_promise_executor<T, Blocking>,
                    execution::context_t> {
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_valid = true);
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_noexcept = true);
  typedef boost::asio::system_context& result_type;
};

#endif  // !defined(BOOST_ASIO_HAS_DEDUCED_QUERY_MEMBER_TRAIT)

#if !defined(BOOST_ASIO_HAS_DEDUCED_REQUIRE_MEMBER_TRAIT)

template <typename T, typename Blocking>
struct require_member<
    boost::asio::internal::fiber_promise_executor<T, Blocking>,
    execution::blocking_t::possibly_t> {
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_valid = true);
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_noexcept = true);
  typedef boost::asio::internal::fiber_promise_executor<
      T, execution::blocking_t::possibly_t>
      result_type;
};

template <typename T, typename Blocking>
struct require_member<
    boost::asio::internal::fiber_promise_executor<T, Blocking>,
    execution::blocking_t::never_t> {
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_valid = true);
  BOOST_ASIO_STATIC_CONSTEXPR(bool, is_noexcept = true);
  typedef boost::asio::internal::fiber_promise_executor<
      T, execution::blocking_t::never_t>
      result_type;
};

#endif  // !defined(BOOST_ASIO_HAS_DEDUCED_REQUIRE_MEMBER_TRAIT)

}  // namespace traits

#endif  // !defined(GENERATING_DOCUMENTATION)

}  // namespace asio
}  // namespace boost

#include <boost/asio/detail/pop_options.hpp>

#endif  // ASYIK_IMPL_USE_FIBER_FUTURE_HPP
