<img src="docs/libasyik.png" width=420>

[![okyfirmansyah](https://circleci.com/gh/okyfirmansyah/libasyik.svg?style=shield)](<LINK>) [![codecov](https://codecov.io/gh/okyfirmansyah/libasyik/branch/master/graph/badge.svg)](https://codecov.io/gh/okyfirmansyah/libasyik)

**Libasyik** is C++ microframework for highly scalable concurrent programming based on [**boost::fiber**](https://www.boost.org/doc/libs/1_73_0/libs/fiber/doc/html/index.html) and [**boost::asio**](https://www.boost.org/doc/libs/1_73_0/doc/html/boost_asio.html). 

Basic features containing web server, websockets, http clients, and database access functions.

```c++
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

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
   - Programming in synchronous fashions, but lightweight enough to implement **1 connection=1 Process** to simplify network programming models
   - Wraps **boost::asio** and **boost::beast** asynchronous APIs into "synchronous" libasyik's API's that generally much easier to use and programs
 - HTTP server easy routing
   - Pattern matching with arguments supported
   - Create websocket server as easy as HTTP request handlers
 - SQL access APIs based on **SOCI**
   - Support **PostgreSQL** and **SQLite** backends
   - Wrapped as "synchronous" APIs for lightweight threads/fibers
 - Build in logging based on [Aixlog](https://github.com/badaix/aixlog)
   
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

#### SQL Database
```c++
#include "libasyik/sql.hpp"
...
void some_handler(asyik::service_ptr as)
{
    auto pool = make_sql_pool(asyik::sql_backend_sqlite3, "test.db", 1);
    auto ses = pool->get_session(as);

    ses->query(R"(CREATE TABLE IF NOT EXISTS persons (id int,
                                                      name varchar(255));)");
    int id=1;
    int out_id;
    std::string name;
    ses->query("select * from persons where id=:id", soci::use(id), 
                                                     soci::into(out_id),
                                                     soci::into(name));
}
```

## More Documentations(Work In Progress)
 - Features:
   - [Logging](docs/logging.md)
   - [Fiber Framework](docs/service.md)
   - [HTTP/Websocket](docs/http.md)
   - [SOCI/SQL](docs/sql.md)
 - Design/Rationale
   - [Why Fiber?](docs/why.md)
   - [Multithreading approach in Libasyik ](docs/designs.md)
   - [Integrating your library to Libasyik](docs/integration.md)
 
 
## How to Build

### Requirements

 - C++ compiler with good C++17 support (tested with GCC 7.5.0)
 - Boost library with Boost::context, Boost::fiber, and Boost::asio(Boost version 1.70.0 or above recommended)
 - CMake > 3.12
 - SOCI with SQLite and PostgreSQL backend and required low level libraries
 - See [Dockerfile](Dockerfile) to have a direct example of build environment

### Building and Including in a Project

After all environment requirements demonstrated in [Dockerfile](Dockerfile) are done, do this following steps:

```
 cd ~
 git clone https://github.com/okyfirmansyah/libasyik
 cd ~/libasyik
 git submodule update --init --recursive
 mkdir build
 cd build
 cmake ..
 make -j4
 make install
```
After that, Libasyik is now ready to be included in new project.
As example use following CMakeLists.txt template to invoke Libasyik using **find_package()**:
```
cmake_minimum_required(VERSION 3.14)
project(test_asyik)
set(CMAKE_CXX_STANDARD 17)

add_executable(${PROJECT_NAME} test.cpp) # add more source code here

#######
#include other dependencies
#######
find_package(libasyik)
if(libasyik_FOUND)
    target_include_directories(${PROJECT_NAME} PUBLIC ${libasyik_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} libasyik)
endif()

find_package(Boost COMPONENTS context fiber REQUIRED)
if(Boost_FOUND)
    target_include_directories(${PROJECT_NAME} PUBLIC ${Boost_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} Boost::fiber Boost::context)
endif()


find_package(SOCI REQUIRED)
if(SOCI_FOUND)
    target_include_directories(${PROJECT_NAME} PUBLIC /usr/include/postgresql)
    target_include_directories(${PROJECT_NAME} PUBLIC /usr/local/include/soci)
    target_include_directories(${PROJECT_NAME} PUBLIC /usr/local/include/soci/postgresql)
    target_include_directories(${PROJECT_NAME} PUBLIC /usr/local/include/soci/sqlite3)
    target_link_libraries(${PROJECT_NAME} SOCI::soci_core SOCI::soci_postgresql SOCI::soci_sqlite3)
endif()

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)

find_package(OpenSSL REQUIRED)
target_link_libraries(${PROJECT_NAME} OpenSSL::SSL)
```
