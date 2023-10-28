## Using with the rest of Boost::asio or Asio-based library
Libasyik has the best integration capability with `Asio` and `Asio-based` asynchronous model.

Libasyik provide class `asyik::use_fiber_future` as a completion token that can be used to any asynchronous functions that support `boost::asio` completion token. For example, we make Asio's `async_write_some()` to be compatible with our fiber environment:
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

## Using with other external library
In order to use Libasyik with other external library, we can wrap the API from those library so it can integrate seamlessly with the rest of fiber system design. In a such way that the integrated API will **help fiber yield execution** to the other fibers during the API's execution.

### Wrap library blocking functions
Suppose we have POSIX `fread()` function that we want to call from the inside of fiber/HTTP handlers. The correct way to wrap is:
```c++
int main()
{
    auto as = asyik::make_service();

    auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

    server->on_http_request("/sse", "GET", [server](auto req, auto args) {
        // do not call fread() directly, as it will starve the only thread we have.
        //
        // Instead wrap like this:
        auto result=as->async([]()->std::string
        {
            ...
            fread(f, result_buffer, size);
            return std::to_string(result_buffer);
        });
        req->response.body = result.get();
        req->response.result(200);
    });

    as->run();
}
```

### Wrap library asynchronous functions
if we have a library function, for example, `void http_async_read(buffer, length, callback)`, we can wrap the API using this pattern:
```c++
auto asyik_http_async_read(void* buffer, size_t length) -> boost::fibers::future<size_t>
{
  boost::fibers::promise<size_t> promise;
  auto future = promise.get_future();

  http_async_read(buffer, length,
      [prom = std::move(promise)](size_t num_read) mutable {
        prom.set_value(num_read);
      });

  return future;
};

```