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

  auto server = asyik::make_http_server(as, "0.0.0.0", 4004);

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

  auto server = asyik::make_http_server(as, "0.0.0.0", 4004);

  server->on_websocket("/<int>/test/<string>", [](auto ws, auto args) {
    ws->send_string(args[1]);
    ws->send_string(args[2]);

    auto s = ws->get_string();
    ws->send_string(s);

    ws->close(websocket_close_code::normal, "closed normally");
  });

  server->on_http_request("/post-only/<int>/name/<string>", "POST", [](auto req, auto args) {
    LOG(INFO) << "\ntarget()=" << req->target() << "\n";
    LOG(INFO) << "\nmethod_string()=" << req->method() << "\n";
    LOG(INFO) << "\nreq.base().at('Host')=" << req->headers["host"] << "\n";
    LOG(INFO) << "\nreq.base().at('x-test')=" << req->headers["x-test"] << "\n";

    LOG(INFO) << "\nreq.base().at('content-length')=" << req->headers["content-length"] << "\n";
    LOG(INFO) << "\nreq.base().at('content-type')=" << req->headers["content-type"] << "\n";
    LOG(INFO) << "\nBody=\n"
              << req->body << "\n";

    req->response.headers.set("x-test-reply", "amiiin");
    req->response.headers.set("content-type", "text/json");
    req->response.body = req->body + "-" + args[1] + "-" + args[2];
    req->response.result(200);
  });

  server->on_http_request("/any/<int>/", [](auto req, auto args) {
    LOG(INFO) << "\ntarget()=" << req->target() << "\n";
    LOG(INFO) << "\nmethod_string()=" << req->method() << "\n";
    LOG(INFO) << "\nreq.base().at('Host')=" << req->headers["host"] << "\n";
    LOG(INFO) << "\nreq.base().at('x-test')=" << req->headers["x-test"] << "\n";

    LOG(INFO) << "\nreq.base().at('content-length')=" << req->headers["content-length"] << "\n";
    LOG(INFO) << "\nreq.base().at('content-type')=" << req->headers["content-type"] << "\n";
    LOG(INFO) << "\nBody=\n"
              << req->body << "\n";

    req->response.headers.set("x-test-reply", "amiiin");
    req->response.headers.set("content-type", "text/json");
    std::string s{req->method()};
    req->response.body = req->body + "-" + s + "-" + args[1];
    req->response.result(200);
  });

  // asyik::sleep_for(std::chrono::milliseconds(500));
  // http_request req();
  // req.headers.set("x-test", "hello");
  // req.body = "check-";
  // asyik::http_easy_request(as, "POST", "127.0.0.1:4004/post-only/69/name/listed", req);

  // auto client = asyik::make_http_connection(as, "127.0.0.1", "80");
  // asyik::http_request_ptr req("GET", "/");
  // req.set_header();
  // req.set_body();
  // asyik::http_respond_ptr resp = client->send_request(req);
  // resp->get_http_status();
  // resp->get_http_body();
  // resp->get_header();

  // auto {status, body} = asyik::http_easy_request(as, "127.0.0.1:80/trr6/ytt6", req_body);
  // auto {status, body} = asyik::http_easy_request(as, "127.0.0.1:80/trr6/ytt6", req_body, req_header);
  // auto {status, body, header} = asyik::http_easy_request(as, "127.0.0.1:80/trr6/ytt6", req_body, req_header);

  as->stop();
  as->run();
}
