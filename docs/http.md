Libasyik's HTTP layer is provided by [Boost::Beast](https://www.boost.org/doc/libs/1_73_0/libs/beast/doc/html/index.html) and [Boost::Asio](https://www.boost.org/doc/libs/1_73_0/doc/html/boost_asio.html).

HTTP server and client functionalities are wrapped as fiber APIs that greatly simplify network programming model by synchronous-like usage and highly concurrent Fiber threading system.

#### Basic HTTP with arguments
```c++
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

void main()
{
    auto as = asyik::make_service();
    auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

    // accept string argument
    server->on_http_request("/name/<string>", "GET", [](auto req, auto args)
    {
      req->response.body = "Hello " + args[1] + "!";
      req->response.result(200);
    });
    
    // accept string and int arguments
    server->on_http_request("/name/<string>/<int>", "GET", [](auto req, auto args)
    {
      req->response.body = "Hello " + args[1] + "! " + "int=" + args[2];
      req->response.result(200);
    });
    
    as->run();
  }
```

#### Set and Get HTTP Payload and Headers
```c++
void main()
{
    auto as = asyik::make_service();
    auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

    // accept string argument
    server->on_http_request("/name/<string>", "POST", [](auto req, auto args)
    {
      req->response.body = "{\"value\":\"Hello " + args[1] + "!\"}";
      LOG(INFO) << "X-Asyik=" << req->headers["x-asyik"] <<"\n";
      LOG(INFO) << "Body=" << req->body <<"\n";
      
      req->response.headers.set("x-asyik-reply", "ok");
      req->response.headers.set("content-type", "text/json");
      req->response.result(200); 
    }); // other standard headers like content-length is set by library
    
    as->run();
  }
```

#### HTTP Client
```c++
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

int main()
{
  auto as = asyik::make_service();

  // spawn new fiber
  as->execute( 
    [&completed, as]() 
    {
      auto req = asyik::http_easy_request(as, "GET", "https://tls-v1-2.badssl.com:1012/");

      if(req->response.result()==200)
      {
        LOG(INFO)<<"Body="<<req->response.body<<"\n";
        LOG(INFO)<<"request success!\n";
      }
      
      // post with payload and additional headers
      auto req2 = asyik::http_easy_request(as, "POST", "http://some-host/api", 
                                           "this is payload", 
                                           {
                                            {"x-test", "ok"}  //headers
                                           });

      if(req2->response.result()==200)
      {
        LOG(INFO)<<"Body="<<req2->response.body<<"\n";
        LOG(INFO)<<"request success!\n";
      }

      // do client request again, but with TIMEOUT=10s (default is 30s)
      auto req3 = asyik::http_easy_request(as, 
                                           10000, //timeout in ms
                                           "POST", "http://some-host/api", 
                                           "this is payload", 
                                           {
                                            {"x-test", "ok"}  //headers
                                           });

      if(req3->response.result()==200)
      {
        LOG(INFO)<<"Body="<<req2->response.body<<"\n";
        LOG(INFO)<<"request success!\n";
      }
      
      as->stop();
    }
  );
  
  // start service and fiber scheduler
  as->run();
 }
```

#### Websockets Server
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

#### Websocket Client
```c++
void main()
{
    auto as = asyik::make_service();

    as->execute([=]() {
      // connect to echo test server
      asyik::websocket_ptr ws = asyik::make_websocket_connection(as, "wss://echo.websocket.org");

      ws->send_string("halo");
      auto s = ws->get_string();
      if (!s.compare("halo"))
        LOG(INFO)<<"echo success!\n";

      ws->close(asyik::websocket_close_code::normal, "closed normally");
      as->stop();
    });

    as->run();
}
```

#### Sending/Reading Binary Buffer
Libasyik support basic sending/reading binary buffer in websocket connection as std::vector<uint8_t>
```c++
void main()
{
    auto as = asyik::make_service();

    as->execute([=]() {
      // connect to echo test server
      asyik::websocket_ptr ws = asyik::make_websocket_connection(as, "wss://echo.websocket.org");

      std::vector<uint8_t> buff;
      // fill the binary buff here
      ...
      ws->write_basic_buffer(buff);
      
      // reading binary
      std::vector<uint8_t> read_buff;
      read_buff.resize(1024); // prepare max size
      auto read_size=ws->read_bassic_buffer(read_buff); // if read_size>max size, exception will be thrown
      read_buff.resize(read_size); // trim size based on actual received message
      ...
    });

    as->run();
}
```

#### Create SSL Server
To create HTTPS server, use the same templates but now using **make_https_server()**, for e.g:
```c++
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

int main()
{
    auto as = asyik::make_service();

    // The SSL context is required, and holds certificates
    ssl::context ctx{ssl::context::tlsv12};

    // This holds the self-signed certificate used by the server
    load_server_certificate(ctx);

    auto server = asyik::make_https_server(as, std::move(ctx), "127.0.0.1", 443);

    server->on_websocket("/ws", [](auto ws, auto args) {
      auto s = ws->get_string();
      ws->send_string(s);

      ws->close(websocket_close_code::normal, "closed normally");
    });

    server->on_http_request("/", "GET", [](auto req, auto args) {
      req->response.body = "hello world";
      req->response.result(200);
    });
    
    as->run();
}
```
Please take a look at [Beast's example](https://www.boost.org/doc/libs/1_73_0/libs/beast/example/common/server_certificate.hpp) for an example on how to perform SSL context creation/**load_server_certificate()**.

### Create multi-thread server
Libasyik use **explicitly single thread** model, meaning an instance of `asyik::service` used to create HTTP server instance will handles incoming connection by the same thread the `as->run()` is called.

You can still create a HTTP server that can accept connection in multi-thread fashion by implementing multiple `std::thread` each having their own `asyik::service` and a HTTP server instance. To avoid IP bind conflict, the HTTP server API allow reusing the same listening address and port, utilizing Linux kernel's [SO_REUSEPORT](https://lwn.net/Articles/542629/):
```c++
int main()
{
  for (int i = 0; i < 8; i++) {
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
Obviously, since all HTTP handler now run in one of multiple threads, any access to shared variables or memory regions should be protected with synchronizations.

#### Set Incoming Request Body and Header Size Limits
To protect against unbounded incoming data size(overflow or out of memory error), by default, incoming request header and body size are set to both 1MB each.  You can override these two settings using following:
```c++
int main()
{
    ...
    
    server->on_http_request("/", "GET", [](auto req, auto args) {
      req->response.body = "hello world";
      req->response.result(200);
    });

    server->set_request_header_limit(1*1024*1024); // set incoming header max size to 1MB
    server->set_request_body_limit(8*1024*1024);   // set incoming body max size to 8MB
    
    ...
}
```

#### Apply Rate Limiter to HTTP API
We can use Libasyik's implementation of [leaky bucket](rate_limit.md) algorithm:
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

#### Advanced Topic: Handle HTTP Connection and Its Responses Manually
Sometimes we want to do something different that simple HTTP request and response, one use case is doing server side event stream(SSE) so the client can have mutiple datas/events, transmitted in realtime and on a single, long connection.

Currently, libasyik only support limited APIs to support this, basically you handle HTTP request using a handler like before, then acquire underlying TCP or SSL stream from the request object, finally we call **req->activate_direct_response_handling()** to signal framework that the HTTP connection is now under handler's fiber control.

Thanks to fiber programming model, it's easy to switch from usual short-lived handler into long-lived fiber connection handler. For example, consider a SSE server:
```c++
int main()
{
    auto as = asyik::make_service();

    auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

    server->on_http_request("/sse", "GET", [server](auto req, auto args) {
        auto connection = req->get_connection_handle(server);
        auto &stream = connection->get_stream();
        req->activate_direct_response_handling();

        // at this point we own connection/stream directly..
        std::string s= "HTTP/1.1 200 OK\r\n"
                       "Connection: keep-alive\r\n"
                       "Content-type: text/event-stream\r\n\r\n"
                       "retry: 5000\r\n\r\n";

        // note: libasyik provide asyik::use_fiber_future to turn
        // any boost asio's API into yield-enable fiber future
        stream.async_write_some(asio::buffer(s.data(), s.length()), asyik::use_fiber_future).get();

        // now sending stream of events indefinitely
        int i=0;
        for(;;)                    
        {
          std::string body="event: foo\r\n"
                           "data: halo "+std::to_string(i++)+"\r\n"
                           "\r\n";
          stream.async_write_some(asio::buffer(body.data(), body.length()), asyik::use_fiber_future).get();
          asyik::sleep_for(std::chrono::seconds(1));
        }
    });

    as->run();
}
```

When we test the SSE endpoint, the request and event stream responses will have something like:
```
[request]
GET /sse HTTP/1.1
Host: localhost
Accept: text/event-stream

[responses]
HTTP/1.1 200 OK
Connection: keep-alive
Content-type: text/event-stream

retry: 5000

event: foo
data: halo 0

event: foo
data: halo 1

event: foo
data: halo 2

[repeat indefinitely each 1 secs...]
```

#### Advanced Topic: Set Websocket Idle Timeout and Keep-alive Ping
Any WebSocket client and server application can tell the healthiness of a particular connection by observing that data transmission and reception happening without exception or error. Now, the tricky part is when doing/waiting data reception as we can not immediately tell whether there is indeed no data available from another end, or the connection itself already falling apart.

One way to detect adverse connection problems is using idle timeout. Here, the party that doing data reception will close the connection when it does not receive data after a certain period of time.

You can use **set_idle_timeout(sec)** to set a particular connection's idle timeout:
```c++
asyik::websocket_ptr ws = asyik::make_websocket_connection(as, "wss://echo.websocket.org");
ws->set_idle_timeout(5);

// if after 5s, we don't get any data, this line will throw exception
auto s = ws->get_string();
```

You can also set the timeout for server connection as well:
```c++
server->on_websocket("/ws_endpoint", [](auto ws, auto args) {
  ws->set_idle_timeout(2);

  // if after 2s, no data is received from the client, this line will throw timeout exception
  auto s = ws->get_string();
  ...
 });
 ```
 
#### Configuring Keep-alive mechanism
In most cases, it's not really convenient to set idle timeout alone, for e.g some WebSocket use case scenario can have no data in a minute while the transmission itself is still intact, so killing and renewing the connection will be inefficient. With the keep-alive mechanism, you can push underlying WebSocket protocol implementation to send and receive control messages, so it will be counted as aliveness/healthiness check of the connection.

When waiting for incoming data, the WebSocket implementation will send PING behind the scene and expect PONG reply from the other end, this invisible control message exchange will prolong the timeout period:
```c++
asyik::websocket_ptr ws = asyik::make_websocket_connection(as, "wss://echo.websocket.org");
ws->set_idle_timeout(5);
ws->set_keepalive_pings(true);

// during the 5s window, if there is no data received, PING-PONG exchange will be performed to proble the connection liveness
auto s = ws->get_string();
```

**note:** If the other end that expected to reply PING with PONG is also libasyik WebSocket(based on boost::beast), then the other end will have to be in the process of receiving data as well, otherwise the PONG will not be emitted.

### Extracting boost.url_view in http handler
Boost.url_view extracts and organizes information from a given URL. This information includes the protocol (e.g. https), domain name (e.g. www.example.com), path (e.g. /products/category1), and query parameters (e.g. ?sort=price&filter=new).
Additional resources: [Boost::Url](https://www.boost.org/doc/libs/master/libs/url/doc/html/url/overview.html#url.overview.quick_look.accessing)
```c++
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

int main()
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4006);

  server->on_http_request("/name/<string>", "GET", [](auto req, auto args) {
    req->response.headers.set("x-test-reply", "amiiin");
    req->response.headers.set("content-type", "text/json");

    auto uv = req->get_url_view();
    
    for (auto x : uv.params()) {
      LOG(INFO) << x.key << "=" << x.value << " ";
    }
    
    req->response.body = "Hello " + args[1] + "!";
    req->response.result(200);
  });

  as->execute([as]() {
    auto req =
        asyik::http_easy_request(as, "GET", "http://127.0.0.1:4006/name/999/");

    assert(req->response.result() == 200);
    assert(!req->response.body.compare("GET-999-"));
    assert(!req->response.headers["x-test-reply"].compare("amiiin"));

    req = asyik::http_easy_request(
        as, "GET", "http://127.0.0.1:4006/name/999/?dummy1=2&dummy2=haha");

    assert(req->response.result() == 200);
    assert(!req->response.body.compare("GET-999-dummy1=2,dummy2=haha,"));
    assert(!req->response.headers["x-test-reply"].compare("amiiin"));

    as->stop();
  });

  as->run();
}
```