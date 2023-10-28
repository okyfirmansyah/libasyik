# System Design
`Libasyik::service` use explicitly single thread and **share-nothing** philosophy by strictly assigning each `as->run()` to a thread. Any fiber that being spawned by `as->execute()` will be executed in that thread in 1:N concurrency fashion.

Supposed a basic Libasyik HTTP server:
```c++
void main()
{
    auto as = asyik::make_service();
    auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

    // serve http request(can coexists with websocker endpoints)
    server->on_http_request("/hello", "GET", [](auto req, auto args)
    {
      req->response.body = "Hello world!";
      req->response.result(200);
    });
    
    // serve websocket
    server->on_websocket("/websocket", [](auto ws, auto args) 
    { 
      // executed as lightweight process/fiber
      while(1)
      {
        auto s = ws->get_string(); 
        ws->send_string(s); //echo
      }

      ws->close(asyik::websocket_close_code::normal, "closed normally");
    });

    as->run();
}
```

In the example above, all the incoming requests will be handled as fiber in the main thread. No multithreading is necessary.

Now, if we expand the example, somehow in the websocket handling we need to call external blocking function that cannot yield/give up execution, the correct way to wrap the calling is:
```c++
// serve websocket
    server->on_websocket("/websocket", [](auto ws, auto args) 
    { 
      // executed as lightweight process/fiber
      while(1)
      {
        auto s = ws->get_string(); 
        auto result = asyik::async([]()
            {
                uint8_t buffer[SIZE];
                fread_some(f, buffer, sizeof(buffer));
                return std::to_string(buffer);
            });
        ws->send_string(result.get);
      }

      ws->close(asyik::websocket_close_code::normal, "closed normally");
    });
```

The blocking function will be isolated to an available thread pool, while the current fiber in the main thread will give execution to other ready fibers. So the main function/server is still single thread, but only very specific execution is dispatched to one of the thread pool:

<img src="design.png" width="720" >

# Multithreading Approach
As mentioned, Libasyik's service is stricly bounded to the single server and use shared-nothing in handling requests in multi-thread fashion. Some reasoning:
 - Multi-threading is not an immediate need in the most basic, if not majority, use cases. Especially if I/O is more concerned
 - By assuming single thread for particular service, we are not obliged to use multithreading synchronizations. We can still achieve multi-tasking and concurrency by spawning as many fibers as we like that run under single thread.
 - On top of convenience on avoiding synchronizations, single thread arrangement also reduce performance overhead on waiting mutex, context-switching, and ultimately cache invalidation or memory barrier/flushing operations. No memory barrier ensure that each core CPU executing the fibers can enjoy most of performance benefit of L1-L3 caching
 - Now if you must use multi-threading to handle incoming requests, using multiple `asyik::service` in different threads, and ensure that all process **uses as few shared variables as possible** will achieve both performance and convenience benefit like single thread program, as per **share-nothing architecture**

<img src="bounded_fibers.png" width="640" >

If in a multi-threading environment, interaction between thread is unavaoidable, we can use [boost::fibers synchronization API](https://www.boost.org/doc/libs/1_83_0/libs/fiber/doc/html/fiber/synchronization.html) to help, not only protect shared variable from racing condition, but also utilize [fiber::channel](https://www.boost.org/doc/libs/1_83_0/libs/fiber/doc/html/fiber/synchronization/channels.html) to communicate between fibers.

<img src="free_fibers.png" width="480" >


# Multithreading on Networking Approach
One common pattern to distribute incoming connection to multiple thread handlers is to use single thread as acceptor and then dispatch to one of the handler threads:

<img src="single_acceptor.png" width="640" >

This approach has a drawbacks that the acceptor can be the hotspot and in some edge case(for extreme number of requests) can be a bottleneck. Transfering between acceptor thread to the handler thread will also add context-switching overhead.

### Libasyik's approach
Libasyik use **shared-nothing** philosophy, and this also including how it handles multithreading server. Instead of assigning acceptor thread as the front line, we turn kernel to act as load balancer via `SO_REUSEPORT` feature in the linux kernel:

<img src="multiple_acceptors.png" width="540" >

With this architecture, each thread handler can bind directly to the same `address:port` and process the requests in independent and isolated fashion:
```c++
int main()
{
  for (int i = 0; i < 8; i++) {
    // each thread will have isolated HTTP server
    std::thread t([&stopped]() {
      auto as = asyik::make_service();

      // create http server with reusable port=true
      auto server = asyik::make_http_server(as, "127.0.0.1", 4004, true);
      server->on_http_request("/flag", "GET", [&flag_http](auto req, auto args) {
                                req->response.body = "ok";
                                req->response.result(200);
                              });

      as->run();

    }
    );
    t.detach();
  }

  ...
}
```