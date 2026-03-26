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

> **Note on `args` indexing:** `args` is a `std::vector<std::string>` populated
> from the regex match. `args[0]` contains the **full regex match** (the entire
> matched URL path), while `args[1]`, `args[2]`, etc. contain the captured
> groups corresponding to `<int>`, `<string>`, or `<path>` tags. Always start
> reading parameters from `args[1]`.

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

#### Route Registration: Tags vs Raw Regex

`on_http_request(route_spec, ...)` uses **route tags** (`<int>`, `<string>`,
`<path>`). All regex special characters in the route spec are escaped
automatically before tags are substituted. This means passing a raw regex
like `/api/(\d+)/file` will **silently fail** — the parentheses and backslash
are escaped, and the route will never match.

For raw regex patterns, use `on_http_request_regex()` directly:

```c++
// WRONG — parens and \d are escaped, route never matches:
server->on_http_request("/api/(\\d+)/file", "GET", handler);

// CORRECT — use on_http_request_regex:
server->on_http_request_regex(
    std::regex("^/api/(\\d+)/file$"), "GET", handler);

// CORRECT — use route tags:
server->on_http_request("/api/<int>/file", "GET", handler);
```

A warning is logged when `route_spec_to_regex()` detects raw regex characters
in the spec string, to help catch this mistake early.

#### Route Ordering and Priority

Routes are matched in **registration order** — the first route whose pattern
matches the request wins. This is important when using `serve_static()` with
a catch-all prefix like `"/"`.

All registration methods (`on_http_request`, `on_http_request_regex`,
`on_websocket`, `on_websocket_regex`) accept an optional `insert_front`
parameter (default `false`). When `true`, the route is inserted at the
beginning of the route list instead of appended:

```c++
auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

// This catch-all is registered first
server->serve_static("/", "/var/www/html");

// But this route is inserted at the front, so it matches before serve_static
server->on_http_request("/api/<string>", "GET", [](auto req, auto args) {
    req->response.body = "api: " + args[1];
    req->response.result(200);
}, /*insert_front=*/true);
```

**Best practice:** Register specific API routes first, then call
`serve_static()` last so it only handles requests that no explicit route
matched.

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

#### Static File Serving

Libasyik can serve an entire directory tree of static files with a single call to **`serve_static()`**. Include `libasyik/http.hpp` — no extra header is required.

```c++
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

int main()
{
    auto as = asyik::make_service();
    auto server = asyik::make_http_server(as, "127.0.0.1", 8080);

    // All GET /static/* requests are served from /var/www/html
    server->serve_static("/static", "/var/www/html");

    as->run();
}
```

Every `GET` request whose URL starts with the given prefix is mapped to the corresponding file under `root_dir`. For example, `GET /static/css/app.css` reads `/var/www/html/css/app.css`.

##### What is handled automatically

| Feature | Behaviour |
|---|---|
| **MIME types** | Derived from the file extension (`.html`, `.css`, `.js`, `.json`, `.png`, `.svg`, `.woff2`, `.wasm`, … — falls back to `application/octet-stream`) |
| **ETag** | A quoted `"mtime-size"` tag is sent; `If-None-Match` matching returns `304 Not Modified` |
| **Last-Modified** | RFC 7231 date header is sent; `If-Modified-Since` matching returns `304 Not Modified` |
| **Range requests** | `Range: bytes=A-B` returns `206 Partial Content` with the requested byte slice |
| **Directory URLs** | A URL that resolves to a directory is redirected to the configured index file (`index.html` by default) |
| **Path-traversal guard** | `realpath()` is used to canonicalise every path; requests that escape `root_dir` are rejected with `403 Forbidden` |
| **Query strings** | Query and fragment portions of the URL are stripped before the file path is resolved |
| **Percent-encoding** | `%XX` sequences in the URL path are decoded before the file lookup |

##### Customising behaviour with `static_file_config`

```c++
asyik::static_file_config cfg;
cfg.enable_etag          = true;               // default: true
cfg.enable_last_modified = true;               // default: true
cfg.enable_range         = true;               // default: true
cfg.cache_control        = "no-cache";         // default: "public, max-age=3600"
cfg.index_file           = "index.html";       // default: "index.html"

server->serve_static("/assets", "./public", cfg);
```

##### Multiple prefixes and HTTPS

`serve_static()` can be called multiple times to map different URL prefixes to different directories. It works identically with `make_https_server()`:

```c++
server->serve_static("/static",     "/var/www/html");
server->serve_static("/downloads",  "/srv/files");

// Works the same way on an HTTPS server
auto https_server = asyik::make_https_server(as, std::move(ssl_ctx), "0.0.0.0", 443);
https_server->serve_static("/static", "/var/www/html");
```

##### HTTP response codes

| Condition | Status |
|---|---|
| File found and served in full | `200 OK` |
| Conditional GET matches (ETag / Last-Modified) | `304 Not Modified` |
| Valid `Range` header | `206 Partial Content` |
| File or directory not found | `404 Not Found` |
| Path-traversal attempt or directory without index file | `403 Forbidden` |
| Malformed `Range` header | `400 Bad Request` |

#### HTTP Client with Digest Authentication

The HTTP client automatically handles servers that require [HTTP Digest Authentication](https://developer.mozilla.org/en-US/docs/Web/HTTP/Authentication#digest_authentication_scheme). Embed credentials directly in the URL using the standard `user:password@host` format. When the server replies with `401 Unauthorized`, the client retries the request automatically with the correct `Authorization: Digest …` header — no extra code needed:

```c++
// Credentials embedded in URL: http://username:password@host/path
auto req = asyik::http_easy_request(as, "GET",
    "http://admin:s3cr3t@192.168.1.1/api/status");

if (req->response.result() == 200)
  LOG(INFO) << "authenticated!\n";
```

Digest auth works transparently with `http_easy_request_multipart` as well:

```c++
http_easy_request_multipart(
    as, "GET",
    "http://admin:s3cr3t@192.168.1.1/cgi-bin/stream",
    [](http_request_ptr req) {
      LOG(INFO) << "got part: " << req->multipart_response.body << "\n";
    });
```

**Notes:**
- Only [Digest authentication](https://datatracker.ietf.org/doc/html/rfc7616) (MD5) is handled automatically. Basic auth is **not** injected.
- URL-encoded characters in the password (e.g. `p%40ss` → `p@ss`) are decoded automatically.
- If the server does not return `401`, the request is sent without any auth header, even when credentials are present in the URL.

##### Parsing URLs with embedded credentials

`http_analyze_url()` now also extracts `username`, `password`, and `authority` from the URL. `http_url_scheme` is now a class with accessor methods (previously plain struct fields):

```c++
asyik::http_url_scheme scheme;
asyik::http_analyze_url("https://user:p%40ss@10.0.0.1:8443/path?q=1", scheme);

scheme.host()      // "10.0.0.1"
scheme.port()      // 8443
scheme.username()  // "user"
scheme.password()  // "p@ss"  (URL-decoded)
scheme.is_ssl()    // true
scheme.target()    // "/path?q=1"
scheme.authority() // "user:p%40ss@10.0.0.1:8443"
scheme.get_url_view() // boost::urls::url_view for full URL inspection
```

> ⚠️ **Breaking change**: `http_url_scheme` fields are now private. Replace direct member access (`scheme.host`, `scheme.port`, `scheme.is_ssl`, `scheme.target`) with the corresponding accessor methods (`scheme.host()`, `scheme.port()`, `scheme.is_ssl()`, `scheme.target()`).

#### HTTP Client Get Multipart Response
For now, libasyik only support receiving multipart response in client:
```c++
// http_easy_request() will throw if it get multipart response from the server
auto req = http_easy_request(as, "GET", "https://127.0.0.1:4012/multipart")

// use this instead:
auto req = http_easy_request_multipart(as, "GET", "https://127.0.0.1:4012/multipart",[](http_request_ptr req) {
          // this in line function will be called for each multipart found

          // read main or parent headers:
          std::string main_header = req->response.headers["x-test-header"];

          // read each part's header fields and body:
          std::string header = req->multipart_response.headers["content-type"];
          std::string body = req->multipart_response.body;
        });
```

The following `Content-Type` values are recognised as multipart responses and handled automatically:
- `multipart/x-mixed-replace` (e.g. MJPEG camera streams)
- `multipart/mixed` (RFC 2046 mixed multipart)
- `multipart/form-data`

Multipart interface above will be called incrementally for every part encountered. It is the responsibility of caller to accumulate all the data if required. This interface is designed to support long/persistent connection, for e.g supporting video streaming. In that case, http_easy_request_multipart should be called using the interface with timeout override.

Note also that the interface similar to the http_easy_request, only the difference is the lambda function handling each multipart segments. You can use various override to send data, set headers or set the timeout. The returned req will behave like http_easy_request but only the header will be valid.

To send multipart response from the server, for now libasyik only support it using manual handling on sending data down the stream(see the next section). You must serialize your own multipart HTTP message. You can use Boost::beast serializer library if you need more elaborate functionalities.

#### Getting the Remote Client Address

`http_connection` (obtained through `req->get_connection_handle(server)`) exposes `get_remote_endpoint()`, which returns the TCP endpoint (`boost::asio::ip::tcp::endpoint`) of the connected client:

```c++
server->on_http_request("/info", "GET", [server](auto req, auto args) {
  auto connection = req->get_connection_handle(server);
  auto ep = connection->get_remote_endpoint();

  LOG(INFO) << "request from " << ep.address().to_string()
            << ":" << ep.port() << "\n";

  req->response.body = "your IP: " + ep.address().to_string();
  req->response.result(200);
});
```

For WebSocket handlers, access the connection through `ws->request`:

```c++
server->on_websocket("/ws", [server](auto ws, auto args) {
  auto ep = ws->request->get_connection_handle(server)->get_remote_endpoint();
  LOG(INFO) << "WebSocket connection from " << ep.address().to_string() << "\n";

  while (1) {
    auto s = ws->get_string();
    ws->send_string(s);
  }
});
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