#include <boost/fiber/all.hpp>
#include <iostream>
#include <libasyik/mainloop.hpp>

namespace asyik
{

  void fn(std::string const &str, int n)
  {
    for (int i = 0; i < n; ++i)
    {
      std::cout << i << ": " << str << std::endl;
      boost::this_fiber::sleep_for(std::chrono::seconds(1));
    }
  }

} // namespace asyik
