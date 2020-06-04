#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

using namespace asyik;

// quick hack because somehow Catch2 will miss libasyik's test cases if
// we dont do this:
TEST_CASE("Catch 2 sanity check", "[Catch2]")
{
  auto as = asyik::make_service();

  as->execute([=] {
    LOG(INFO) << "brief fiber";
    asyik::sleep_for(std::chrono::milliseconds(1));
    as->stop();
  });
  as->run();
  REQUIRE(true);
}

TEST_CASE("Create http server and client, do some websocket communications", "[websocket]")
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

  server->on_websocket("/<int>/name/<string>", [](auto ws, auto args) {
    while (1)
    {
      auto s = ws->get_string();
      ws->send_string(args[1] + "." + s + "-" + args[2]);
    }
  });

  int success = 0;
  for (int i = 0; i < 8; i++)
    as->execute([=, &success]() {
      asyik::sleep_for(std::chrono::milliseconds(50 * i));

      asyik::websocket_ptr ws = asyik::make_websocket_connection(as, "127.0.0.1", "4004", "/" + std::to_string(i) + "/name/hash");

      ws->send_string("halo");
      auto s = ws->get_string();
      if (!s.compare(std::to_string(i) + ".halo-hash"))
        success++;

      ws->send_string("there");
      s = ws->get_string();
      if (!s.compare(std::to_string(i) + ".there-hash"))
        success++;

      ws->close(websocket_close_code::normal, "closed normally");
    });

  // task to timeout entire service runtime
  as->execute([=, &success]() {
    for (int i = 0; i < 200; i++) //timeout 20s
    {
      if (success >= 16)
        break;
      asyik::sleep_for(std::chrono::milliseconds(100));
    }
    as->stop();
  });

  as->run();
  REQUIRE(success == 16);
}

TEST_CASE("Create http server and client, do some http communications", "[http]")
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

  server->on_websocket("/<int>/test/<string>", [](auto ws, auto args) {
    ws->send_string(args[1]);
    ws->send_string(args[2]);

    auto s = ws->get_string();
    ws->send_string(s);

    ws->close(websocket_close_code::normal, "closed normally");
  });

  server->on_http_request("/post-only/<int>/name/<string>", "POST", [](auto req, auto args) {
    req->response.headers.set("x-test-reply", "amiiin");
    req->response.headers.set("content-type", "text/json");
    std::string x_test{req->headers["x-test"]};
    req->response.body = req->body + "-" + args[1] + "-" + args[2] + "-" + x_test;
    req->response.result(200);
  });

  server->on_http_request("/any/<int>/", [](auto req, auto args) {
    req->response.headers.set("x-test-reply", "amiin");
    req->response.headers.set("content-type", "text/json");
    std::string s{req->method()};
    std::string x_test{req->headers["x-test"]};
    req->response.body = req->body + "-" + s + "-" + args[1] + "-" + x_test;
    req->response.result(200);
  });

  asyik::sleep_for(std::chrono::milliseconds(100));
  as->execute([as](){
    auto req = asyik::http_easy_request(as, "POST", "http://127.0.0.1:4004/post-only/30/name/listed", "hehe", {{"x-test", "ok"}});

    REQUIRE(req->response.result()==200);
    std::string x_test_reply{req->response.headers["x-test-reply"]};
    std::string body=req->response.body;
    REQUIRE(!body.compare("hehe-30-listed-ok"));
    REQUIRE(!x_test_reply.compare("amiiin"));

    req = asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/any/999/");
    
    REQUIRE(req->response.result()==200);
    REQUIRE(!req->response.body.compare("-GET-999-"));
    REQUIRE(!req->response.headers["x-test-reply"].compare("amiin"));

    req = asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/any/123/", "", {{"x-test", "sip"}});
    
    REQUIRE(req->response.result()==200);
    REQUIRE(!req->response.body.compare("-GET-123-sip"));
    REQUIRE(!req->response.headers["x-test-reply"].compare("amiin"));

    // negative test
    req = asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/not-found");
    REQUIRE(req->response.result()==404);

    //req = asyik::http_easy_request(as, "GET", "3878sad9das7d97d8safdsfd.com/not-found");
    //REQUIRE(req->response.result()==404);

    as->stop();
  });

  // auto client = asyik::make_http_connection(as, "127.0.0.1", "80");
  // auto req=std::make_shared<http_request>();
  // client->send_request(req);
  
  as->run();
}
