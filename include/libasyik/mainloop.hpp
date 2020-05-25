#ifndef VSCODE_TEMPLATE_MAINLOOP_HPP
#define VSCODE_TEMPLATE_MAINLOOP_HPP
#include <boost/fiber/all.hpp>
#include <string>

namespace asyik {
using fiber = boost::fibers::fiber;

class service {
 public:
  ~service(){};
  service& operator=(const service&) = delete;
  service(const service&) = delete;
  service(service&&) = default;
  service& operator=(service&&) = default;

  template <typename T, typename... V>
  void execute(T&& t, V&&... v)
  {
    boost::fibers::fiber fb([t2 = std::forward<T>(t)](V&&... v) { t2(v...); });
    fb.detach();
  }

  template <typename T, typename... V>
  fiber spawn(T&& t, V&&... v)
  {
    boost::fibers::fiber fb([t2 = std::forward<T>(t)](V&&... v) { t2(v...); });
    return std::move(fb);
  }

  void run();

 private:
  service();

  friend service make_service();
};

service make_service();

template <typename T>
void sleep_for(T&& t)
{
  boost::this_fiber::sleep_for(t);
}

}  // namespace asyik

#endif
