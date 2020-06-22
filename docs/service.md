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
We can spawn fiber using **asyik::service**:
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
To spawn and execute fiber, we will need **asyik::service** instance and **as->run()** as scheduler's main loop.

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
[Channel](https://www.boost.org/doc/libs/1_73_0/libs/fiber/doc/html/fiber/synchronization/channels.html) is fiber's standard way to communicate with each other. To use this function, we can invoke directly from [Boost::Fiber](https://www.boost.org/doc/libs/1_73_0/libs/fiber/doc/html/index.html):
```c++
#include "libasyik/service.hpp"
#include "boost/fiber/all.hpp"

namespace fibers = boost::fibers;

int main()
{
  auto as = asyik::make_service();
  fibers::buffered_channel<std::string> ch(16);

  // channel sender
  as->execute(
    [&ch]()
    {
      asyik::sleep_for(std::chrono::seconds(3));

      ch.push("hello there!");
    }
  );

  // channel receiver
  as->execute(
    [&ch, &as]()
    {
      LOG(INFO)<<"waiting for string..\n";

      std::string str;
      // this block this particular fiber until item arrived(str)
      if(boost::fibers::channel_op_status::closed != ch.pop(str))
      {
        LOG(INFO)<<"received str="<<str<<"\n";
      }

      as->stop();
    }
  );

  // start service and fiber scheduler
  as->run();
 }
 ```
Please see [Boost::Fiber](https://www.boost.org/doc/libs/1_73_0/libs/fiber/doc/html/index.html) documentations to see detailed documentation regarding usage of Channels.

### Wrapping Blocking I/O or CPU-Intensive Routine with async()
Fiber offers great programming model to handle highly concurrent system like connection handlings, not only due to its lightweight nature, especially to handle many persistent connections, but also because of thread-like synchronous program flows that much easier to follow compared to the callback chains and state machine tracker in the asynchronous world.

But fiber being thread-like in the mode of execution does not mean it behave fully like operating system thread or we call it **physical thread** system. Fiber works in **cooperative-preemptive** mode instead of **preemptive** found in the operating system thread. This means that in order for a fiber to give up it's execution, it needs to **yield** explicitly or implicitly when calling libasyik's APIs or boost::fiber's APIs. Otherwise entire **asyik::service** and event loop will be blocked.

Fiber, in its execution, **should not** call any blocking, native synchronous API, or CPU intensive tasks that takes considerably long time. For example, this will blocks not only the fiber, but also entire **asyik::service** main loop and all fibers owned by the service:

```c++
  as->execute(
    []()
    {
      ...
      // synchronois I/O function, will block entire scheduler!
      boost::asio::connect(...); 
      ...
    }
  );
```

```c++
  as->execute(
    []()
    {
      ...
      // This indeed gives up thread execution, 
      // but the entire scheduler in the same thread will be blocked!
      // Use dedicated fiber's API for this kind of functions
      usleep(1000);
      ...   
    }
  );
```


```c++
  as->execute(
    []()
    {
      ...
      // intensive CPU, will blocks scheduler/other fibers
      do_some_AI_deep_learning_sync_process();
      ...   
    }
  );
```

To wrap execution of synchronous I/O functions or CPU intensive tasks, libasyik provide worker thread pooling mechanism. Inspired by [std::async()](https://en.cppreference.com/w/cpp/thread/async), we can simply wrap synchronous process using **asyik::async()**:
```c++
#include "libasyik/service.hpp"

int main()
{
  auto as = asyik::make_service();  

  as->execute(
    [as]()
    {
      asyik::string_view s="hello there\n";

      auto future=
      as->async(
        [&s]()
        {
          LOG(INFO)<<"this string is printed using worker thread:\n";
          LOG(INFO)<<s;
          sleep(3); // simulate blocking process(I/O, OS Scheduler, or CPU intensive)
        }
      );

      future.get(); // wait for async task to finish
      as->stop();
    }
  );

  // start service and fiber scheduler
  as->run();
}
```

Comparable to [std::async()](https://en.cppreference.com/w/cpp/thread/async), we can also return a result from inside **async()** as **fiber::future<>**:
```c++
#include "libasyik/service.hpp"

int main()
{
  auto as = asyik::make_service();  

  as->execute(
    [as]()
    {
      auto future=
      as->async(
        []()
        {
          // this blocking input will be executed
          // in a worker thread, other fibers and scheduler
          // will continue to run
          std::string input;
          std::cout << "enter name: ";
          std::cin >> input;

          return input;
        }
      );

      LOG(INFO) << "async result=" << future.get(); // wait for async task to finish
      as->stop();
    }
  );

  // start service and fiber scheduler
  as->run();
}
```

Output:
```
root@desktop:/workspaces/test/build# ./test
enter name: Oky
2020-06-22 16-42-50.517 [Info] (operator()) async result=Oky
root@desktop:/workspaces/test/build# 
```
