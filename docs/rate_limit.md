This is an implemenation of thread-safe leaky bucket algorithm.

For now this library only support in-RAM implementation(**rate_limiter_memory**).


```c++
#include "libasyik/error.hpp"
#include "libasyik/service.hpp"
#include "libasyik/rate_limit.hpp"

const int quota_burst = 10;   // capacity for allow initial requests burst(bucket size)
const int desired_qps = 30;   // steady state maximum qps
auto as = asyik::make_service();
auto limiter = asyik::make_rate_limit_memory(as, quota_burst, desired_qps);

as->execute([limiter, as]()
{
  // request 1 hit quota for API/bucket "get_status",
  // will return 1 if quota is available, 0 if it's running out
  int granted = limiter->checkpoint("get_status");
  
  // request 3 hits quota for API/bucket "get_status",
  // will return 0, 1, 2, or 3 depending if what quota is currently available
  int granted = limiter->checkpoint("get_status", 3);
  
  // check remaining quota without invoking it
  int remaining = limiter->get_remaining("get_status"); // should return 10-4=6
  
  limiter->reset(); // will reset all rate limiter quota for all buckets
});

as->run();
```

#### Example application in HTTP server
```c++
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"
#include "libasyik/rate_limit.hpp"

void main()
{
    auto as = asyik::make_service();
    auto server = asyik::make_http_server(as, "127.0.0.1", 4004);
    
    const int quota_burst = 10;   // capacity for allow initial requests burst(bucket size)
    const int desired_qps = 30;   // steady state maximum qps

    auto limiter = asyik::make_rate_limit_memory(as, quota_burst, desired_qps);

    // accept string argument
    server->on_http_request("/user_info/<string>", "GET", [limiter](auto req, auto args)
    {
      // this will apply rate limit per API and per User:
      std::string limiter_bucket = "user_info_"+args[1];
      
      if(limiter->checkpoint(limiter_bucket))
      {
        req->response.body = "Ok";
        req->response.result(200);
      }else
      {
        req->response.body = "Too many requests!";
        req->response.result(429);
      }
    });
    
    as->run();
  }
```
