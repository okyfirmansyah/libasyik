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
