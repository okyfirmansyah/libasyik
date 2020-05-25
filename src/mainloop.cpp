#include <aixlog.hpp>
#include <boost/fiber/all.hpp>
#include <iostream>
#include <libasyik/mainloop.hpp>

namespace asyik {

void init()
{
  AixLog::Log::init<AixLog::SinkCout>(AixLog::Severity::trace,
                                      AixLog::Type::normal);
}

void fn(std::string const& str, int n)
{
  for (int i = 0; i < n; ++i) {
    LOG(INFO) << i << ": " << str << std::endl;
    boost::this_fiber::sleep_for(std::chrono::seconds(1));
  }
}

}  // namespace asyik
