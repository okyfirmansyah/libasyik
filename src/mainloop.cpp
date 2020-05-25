#include <aixlog.hpp>
#include <boost/fiber/all.hpp>
#include <iostream>
#include <libasyik/mainloop.hpp>

namespace asyik {

service::service()
{
  AixLog::Log::init<AixLog::SinkCout>(AixLog::Severity::trace,
                                      AixLog::Type::normal);
  boost::fibers::use_scheduling_algorithm<boost::fibers::algo::round_robin>();
}

service make_service() { return std::move(service()); }

void service::run()
{
  while (1) {
    boost::this_fiber::yield();
    usleep(1000);
  }
}

}  // namespace asyik
