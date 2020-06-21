To manage concurrent network connection processes, Libasyik using user-land threading system based on [Boost::Fiber](https://www.boost.org/doc/libs/1_73_0/libs/fiber/doc/html/index.html). 

Because of its lightweight nature, it is possible to create fibers cheaply while executing network programming in synchronous-like fashion:
```c++
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

// function/thread to handle a connection(websocket persistent connection)
void websocket_handler(asyik::websocket_ptr ws, const asyik::http_route_args &args)
{
  try
  {
    while(1) // echo server
    {
      auto s = ws->get_string();
      ws->send_string(s);
    }
  }
  catch(...) // ends when connection closed
  {
    LOG(INFO)<<"handler completed..\n";
  }
}

int main()
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

  // this will spawn websocket_handler as user-land thread(Fiber)
  // for each incoming websocket connection
  server->on_websocket("/websocket", websocket_handler);

  LOG(INFO)<<"server started..\n";
  as->run();
}
```

### Creating Fiber
You can spawn fiber using **asyik::service**:
```c++
#include "libasyik/service.hpp"


int main()
{
  auto as = asyik::make_service();

  as->execute( // spawn fiber concurrently
    []() 
    {
      while(1)
      {
        asyik::sleep_for(std::chrono::seconds(1));
        LOG(INFO)<<"Fiber A!\n";
      }
    }
  );

  as->execute( // spawn fiber concurrently
    []() 
    {
      while(1)
      {
        asyik::sleep_for(std::chrono::seconds(2));
        LOG(INFO)<<"Fiber B!\n";
      }
    }
  );

  // start service and fiber scheduler
  as->run();
}
```
will results:
```
root@desktop:/workspaces/test/build# ./test 
2020-06-21 05-48-40.060 [Info] (operator()) Fiber A!
2020-06-21 05-48-41.061 [Info] (operator()) Fiber B!
2020-06-21 05-48-41.061 [Info] (operator()) Fiber A!
2020-06-21 05-48-42.061 [Info] (operator()) Fiber A!
2020-06-21 05-48-43.061 [Info] (operator()) Fiber B!
2020-06-21 05-48-43.061 [Info] (operator()) Fiber A!
2020-06-21 05-48-44.061 [Info] (operator()) Fiber A!
```

### Fiber Scheduler
To spawn and execute fiber, you will need **asyik::service** instance and **as->run()** as scheduler's main loop.

One instance of **asyik::service** coresponds to one physical thread execution. So all fiber spawned with **as->execute()** will be executed from that single physical thread.

Fortunately, this have a consequence that every fiber belongs to the same **asyik::service** can have a shared variable/memory without requiring any thread synchronizations:
```c++
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

int main()
{
  auto as = asyik::make_service();
  int completed=0;

  for(int i=0; i<10; i++)// spawn 10 fibers concurrently
  as->execute( 
    [&completed, as]() 
    {
      auto req = asyik::http_easy_request(as, "GET", "https://tls-v1-2.badssl.com:1012/");

      if(req->response.result()==200)
        LOG(INFO)<<"request success!\n";
      completed++;
    }
  );

  as->execute( // watcher fiber
    [&completed, as]() 
    {
      while(1) {
        if(completed==10)
          break;
        asyik::sleep_for(std::chrono::microseconds(10));
      }
      as->stop(); // stop the program
    }
  );

  // start service and fiber scheduler
  as->run();
 }
```

We can also use [Boost::Fiber's synchronization options](https://www.boost.org/doc/libs/1_73_0/libs/fiber/doc/html/fiber/synchronization.html) to support other fiber interaction use cases.

### Communicate Between Fiber using Channel


### Wrapping Blocking I/O or CPU-Intensive Routine with async()

