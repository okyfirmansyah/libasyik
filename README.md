# libasyik
[![okyfirmansyah](https://circleci.com/gh/okyfirmansyah/libasyik.svg?style=shield)](<LINK>)

**Libasyik** is C++ microframework for highly scalable concurrent programming based on **boost::fiber** and **boost::asio**. 

Basic features containing web server, websockets, http clients, and database access functions.

```c++
#include "service.hpp"
#include "http.hpp"

void main()
{
    auto as = asyik::make_service();
    auto server = asyik::make_http_server(as, "127.0.0.1", 8080);

    // serve http request
    server->on_http_request("/hello", "GET", [](auto req, auto args)
    {
      req->response.body = "Hello world!";
      req->response.result(200);
    });
    
    as->run();
  }
```

## Features

 - High scalability
   - Based on lightweight thread using **Boost::Fiber** (easy to create **millions** of fibers!)
   - Programming in synchronous fashions, 1 connection=1 Process, much easier than asynchronous chains
   - Wraps **boost::asio** and **boost::beast** asynchronous APIs into "synchronous" libasyik's API's that generally much easier to use and programs
 - HTTP server easy routing
   - Pattern matching with arguments supported
   - Create websocket server as easy as HTTP request handlers
 - SQL access APIs based on **SOCI**
   - Support **PostgreSQL** and **SQLite** backends
   - Wrapped as "synchronous" APIs for lightweight threads/fibers
 - Very Fast
   
## Examples

#### Basic HTTP with arguments
```c++
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


#### Websockets
```c++
void main()
{
    auto as = asyik::make_service();
    auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

    // serve http request
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

      ws->close(websocket_close_code::normal, "closed normally");
    });

    as->run();
}
```

#### SQL Database
```c++
#include "service.hpp"
#include "sql.hpp"

void main()
{
    auto as = asyik::make_service();

    auto pool = make_sql_pool(sqlite3, "test.db", 1);
    auto ses = pool->get_session(as);

    ses->query(R"(CREATE TABLE IF NOT EXISTS persons (id int,
                                                      name varchar(255));)");
    int id=1;
    int out_id;
    std::string name;
    ses->query("select * from persons where id=:id", use(id), into(out_id), into(name));
}
```

## How to Build

(todo)

### Requirements

 - C++ compiler with good C++17 support (tested with g++>=6.0.0)
 - boost library
 - CMake for build examples
 - Linking with tcmalloc/jemalloc is recommended for speed.

 - Now supporting VS2013 with limited functionality (only run-time check for url is available.)

### Building (Tests, Examples)

Out-of-source build with CMake is recommended.

```
mkdir build
cd build
cmake ..
make
```

You can run tests with following commands:
```
ctest
```


### Attributions

Libasyik uses the following libraries.

    http-parser

    https://github.com/nodejs/http-parser

    http_parser.c is based on src/http/ngx_http_parse.c from NGINX copyright
    Igor Sysoev.

    Additional changes are licensed under the same terms as NGINX and
    copyright Joyent, Inc. and other Node contributors. All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE. 


    qs_parse

    https://github.com/bartgrantham/qs_parse

    Copyright (c) 2010 Bart Grantham
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.


    TinySHA1

    https://github.com/mohaps/TinySHA1

    TinySHA1 - a header only implementation of the SHA1 algorithm. Based on the implementation in boost::uuid::details

    Copyright (c) 2012-22 SAURAV MOHAPATRA mohaps@gmail.com
    Permission to use, copy, modify, and distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.
    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
