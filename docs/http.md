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

#### Advanced Topic: Handle HTTP Connection and Its Responses Manually
Sometimes we want to do something different that simple HTTP request and response, one use case is doing server side event stream(SSE) so the client can have mutiple datas/events, transmitted in realtime and on a single, long connection.

Currently, libasyik only support limited APIs to support this, basically you handle HTTP request using a handler like before, then acquire underlying TCP or SSL stream from the request object, finally we call **req->activate_direct_response_handling()** to signal framework that the HTTP connection is now under handler's fiber control.

Thanks to fiber programming model, it's easy to switch from usual short-lived handler into long-lived fiber connection handler. For example, consider a SSE server:
```c++
// create wrapper for ASIO's stream async_send(SSL/TCP stream)
template <typename Conn, typename... Args>
auto async_send(Conn &con, Args &&... args)
{
    boost::fibers::promise<std::size_t> promise;
    auto future = promise.get_future();

    con.async_write_some(
      std::forward<Args>(args)...,
      [prom = std::move(promise)](const boost::system::error_code &ec,
                                  std::size_t size) mutable {
        if (!ec)
          prom.set_value(size);
        else
          prom.set_exception(
            std::make_exception_ptr(std::runtime_error("send error")));
      });
    return std::move(future);
}

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
        async_send(stream,  asio::buffer(s.data(), s.length())).get();

        // now sending stream of events indefinitely
        int i=0;
        for(;;)                    
        {
          std::string body="event: foo\r\n"
                           "data: halo "+std::to_string(i++)+"\r\n"
                           "\r\n";
          async_send(stream,  asio::buffer(body.data(), body.length())).get();
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