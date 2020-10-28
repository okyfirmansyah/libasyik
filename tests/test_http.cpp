#include "catch2/catch.hpp"
#include "libasyik/service.hpp"
#include "libasyik/http.hpp"

namespace asyik
{
  void _TEST_invoke_http(){};

  TEST_CASE("check routing spec to regex spec conversion")
  {
    std::string spec = "/api/v1/face-match";
    REQUIRE(!internal::route_spec_to_regex(spec).compare(R"(^\/api\/v1\/face\-match\/?(|\?[^\?\s]*)$)"));

    spec = "/api/v1/face-match/";
    REQUIRE(!internal::route_spec_to_regex(spec).compare(R"(^\/api\/v1\/face\-match\/?(|\?[^\?\s]*)$)"));

    spec = "/api/v1/face-match/<int>/";
    REQUIRE(!internal::route_spec_to_regex(spec).compare(R"(^\/api\/v1\/face\-match\/([0-9]+)\/?(|\?[^\?\s]*)$)"));

    spec = "/api/v1/face-match/<string>/n/< int  >";
    REQUIRE(!internal::route_spec_to_regex(spec).compare(R"(^\/api\/v1\/face\-match\/([^\/\?\s]+)\/n\/([0-9]+)\/?(|\?[^\?\s]*)$)"));
  }

  TEST_CASE("check http_analyze_url against URL cases")
  {
    bool result;
    http_url_scheme scheme;

    result = http_analyze_url("simple.com", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("simple.com"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port == 80);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("Http://protspecify.co.id", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("protspecify.co.id"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port == 80);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("https://prothttps.x.y.z/", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("prothttps.x.y.z"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port == 443);
    REQUIRE(scheme.is_ssl);

    result = http_analyze_url("https://protandport.x:69", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("protandport.x"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port == 69);
    REQUIRE(scheme.is_ssl);

    result = http_analyze_url("simple.x.y.z:443/check", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("simple.x.y.z"));
    REQUIRE(!scheme.target.compare("/check"));
    REQUIRE(scheme.port == 443);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("http://simple.co.id:443/check", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("simple.co.id"));
    REQUIRE(!scheme.target.compare("/check"));
    REQUIRE(scheme.port == 443);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("http://10.10.10.1:443/check", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("10.10.10.1"));
    REQUIRE(!scheme.target.compare("/check"));
    REQUIRE(scheme.port == 443);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("http://10.10.10.1/check?hehe", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("10.10.10.1"));
    REQUIRE(!scheme.target.compare("/check?hehe"));

    result = http_analyze_url("http://10.10.10.1/check/?result=ok", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("10.10.10.1"));
    REQUIRE(!scheme.target.compare("/check/?result=ok"));

    result = http_analyze_url("http://10.10.10.1/check/?result=ok&msg=nothing+important&msg2=ok%20done", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("10.10.10.1"));
    REQUIRE(!scheme.target.compare("/check/?result=ok&msg=nothing+important&msg2=ok%20done"));

    result = http_analyze_url("http://10.10.10.1/check/?result=ok&msg=nothing+important&msg2=ok done", scheme);
    REQUIRE(!result);

    result = http_analyze_url("http://he he.com/check/", scheme);
    REQUIRE(!result);

    result = http_analyze_url("http://he he.com:232/check/", scheme);
    REQUIRE(!result);

    result = http_analyze_url("10.10.10.1:443/", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("10.10.10.1"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port == 443);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("10.10.10.2", scheme);
    REQUIRE(result);
    REQUIRE(!scheme.host.compare("10.10.10.2"));
    REQUIRE(!scheme.target.compare("/"));
    REQUIRE(scheme.port == 80);
    REQUIRE(!scheme.is_ssl);

    result = http_analyze_url("invalid://simple.com:443/check", scheme);
    REQUIRE(!result);

    result = http_analyze_url("invalid:simple.com:443/check", scheme);
    REQUIRE(!result);

    result = http_analyze_url("simple.com:443:323/check", scheme);
    REQUIRE(!result);
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

        asyik::websocket_ptr ws = asyik::make_websocket_connection(as, "ws://127.0.0.1:4004/" + std::to_string(i) + "/name/hash");

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

    // check SSL websocket client(using external WSS server)
    as->execute([=, &success]() {
      asyik::sleep_for(std::chrono::milliseconds(50));

      asyik::websocket_ptr ws = asyik::make_websocket_connection(as, "wss://echo.websocket.org");

      ws->send_string("halo");
      auto s = ws->get_string();
      if (!s.compare("halo"))
        success++;

      ws->send_string("there");
      s = ws->get_string();
      if (!s.compare("there"))
        success++;

      ws->close(websocket_close_code::normal, "closed normally");
    });

    // task to timeout entire service runtime
    as->execute([=, &success]() {
      for (int i = 0; i < 200; i++) //timeout 20s
      {
        if (success >= 18)
          break;
        asyik::sleep_for(std::chrono::milliseconds(100));
      }
      as->stop();
    });

    as->run();
    REQUIRE(success == 18);
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
    as->execute([as]() {
      auto req = asyik::http_easy_request(as, "POST", "http://127.0.0.1:4004/post-only/30/name/listed?dummy=hihi", "hehe", {{"x-test", "ok"}});

      REQUIRE(req->response.result() == 200);
      std::string x_test_reply{req->response.headers["x-test-reply"]};
      std::string body = req->response.body;
      REQUIRE(!body.compare("hehe-30-listed-ok"));
      REQUIRE(!x_test_reply.compare("amiiin"));

      req = asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/any/999/?dummy1&dummy2=ha+ha");

      REQUIRE(req->response.result() == 200);
      REQUIRE(!req->response.body.compare("-GET-999-"));
      REQUIRE(!req->response.headers["x-test-reply"].compare("amiin"));

      req = asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/any/123?dummy=ha%20ha", "", {{"x-test", "sip"}});

      REQUIRE(req->response.result() == 200);
      REQUIRE(!req->response.body.compare("-GET-123-sip"));
      REQUIRE(!req->response.headers["x-test-reply"].compare("amiin"));

      req = asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/not-found");
      REQUIRE(req->response.result() == 404);

      // negative tests (TODO)
      //req = asyik::http_easy_request(as, "GET", "3878sad9das7d97d8safdsfd.com/not-found");
      //REQUIRE(req->response.result()==404);

      // SSL Test
      req = asyik::http_easy_request(as, "GET", "https://tls-v1-2.badssl.com:1012/");
      REQUIRE(req->response.result() == 200);
      REQUIRE(req->response.body.length());

      req = asyik::http_easy_request(as, "GET", "https://tls-v1-0.badssl.com:1012/");
      REQUIRE(req->response.result() == 200);
      REQUIRE(req->response.body.length());

      // SSL Negative tests (TODO)

      as->stop();
    });

    // auto client = asyik::make_http_connection(as, "127.0.0.1", "80");
    // auto req=std::make_shared<http_request>();
    // client->send_request(req);

    as->run();
  }

  inline void
  load_server_certificate(boost::asio::ssl::context &ctx)
  {
    std::string const cert =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIDaDCCAlCgAwIBAgIJAO8vBu8i8exWMA0GCSqGSIb3DQEBCwUAMEkxCzAJBgNV\n"
        "BAYTAlVTMQswCQYDVQQIDAJDQTEtMCsGA1UEBwwkTG9zIEFuZ2VsZXNPPUJlYXN0\n"
        "Q049d3d3LmV4YW1wbGUuY29tMB4XDTE3MDUwMzE4MzkxMloXDTQ0MDkxODE4Mzkx\n"
        "MlowSTELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAkNBMS0wKwYDVQQHDCRMb3MgQW5n\n"
        "ZWxlc089QmVhc3RDTj13d3cuZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEBAQUA\n"
        "A4IBDwAwggEKAoIBAQDJ7BRKFO8fqmsEXw8v9YOVXyrQVsVbjSSGEs4Vzs4cJgcF\n"
        "xqGitbnLIrOgiJpRAPLy5MNcAXE1strVGfdEf7xMYSZ/4wOrxUyVw/Ltgsft8m7b\n"
        "Fu8TsCzO6XrxpnVtWk506YZ7ToTa5UjHfBi2+pWTxbpN12UhiZNUcrRsqTFW+6fO\n"
        "9d7xm5wlaZG8cMdg0cO1bhkz45JSl3wWKIES7t3EfKePZbNlQ5hPy7Pd5JTmdGBp\n"
        "yY8anC8u4LPbmgW0/U31PH0rRVfGcBbZsAoQw5Tc5dnb6N2GEIbq3ehSfdDHGnrv\n"
        "enu2tOK9Qx6GEzXh3sekZkxcgh+NlIxCNxu//Dk9AgMBAAGjUzBRMB0GA1UdDgQW\n"
        "BBTZh0N9Ne1OD7GBGJYz4PNESHuXezAfBgNVHSMEGDAWgBTZh0N9Ne1OD7GBGJYz\n"
        "4PNESHuXezAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQCmTJVT\n"
        "LH5Cru1vXtzb3N9dyolcVH82xFVwPewArchgq+CEkajOU9bnzCqvhM4CryBb4cUs\n"
        "gqXWp85hAh55uBOqXb2yyESEleMCJEiVTwm/m26FdONvEGptsiCmF5Gxi0YRtn8N\n"
        "V+KhrQaAyLrLdPYI7TrwAOisq2I1cD0mt+xgwuv/654Rl3IhOMx+fKWKJ9qLAiaE\n"
        "fQyshjlPP9mYVxWOxqctUdQ8UnsUKKGEUcVrA08i1OAnVKlPFjKBvk+r7jpsTPcr\n"
        "9pWXTO9JrYMML7d+XRSZA1n3856OqZDX4403+9FnXCvfcLZLLKTBvwwFgEFGpzjK\n"
        "UEVbkhd5qstF6qWK\n"
        "-----END CERTIFICATE-----\n";

    std::string const key =
        "-----BEGIN PRIVATE KEY-----\n"
        "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDJ7BRKFO8fqmsE\n"
        "Xw8v9YOVXyrQVsVbjSSGEs4Vzs4cJgcFxqGitbnLIrOgiJpRAPLy5MNcAXE1strV\n"
        "GfdEf7xMYSZ/4wOrxUyVw/Ltgsft8m7bFu8TsCzO6XrxpnVtWk506YZ7ToTa5UjH\n"
        "fBi2+pWTxbpN12UhiZNUcrRsqTFW+6fO9d7xm5wlaZG8cMdg0cO1bhkz45JSl3wW\n"
        "KIES7t3EfKePZbNlQ5hPy7Pd5JTmdGBpyY8anC8u4LPbmgW0/U31PH0rRVfGcBbZ\n"
        "sAoQw5Tc5dnb6N2GEIbq3ehSfdDHGnrvenu2tOK9Qx6GEzXh3sekZkxcgh+NlIxC\n"
        "Nxu//Dk9AgMBAAECggEBAK1gV8uETg4SdfE67f9v/5uyK0DYQH1ro4C7hNiUycTB\n"
        "oiYDd6YOA4m4MiQVJuuGtRR5+IR3eI1zFRMFSJs4UqYChNwqQGys7CVsKpplQOW+\n"
        "1BCqkH2HN/Ix5662Dv3mHJemLCKUON77IJKoq0/xuZ04mc9csykox6grFWB3pjXY\n"
        "OEn9U8pt5KNldWfpfAZ7xu9WfyvthGXlhfwKEetOuHfAQv7FF6s25UIEU6Hmnwp9\n"
        "VmYp2twfMGdztz/gfFjKOGxf92RG+FMSkyAPq/vhyB7oQWxa+vdBn6BSdsfn27Qs\n"
        "bTvXrGe4FYcbuw4WkAKTljZX7TUegkXiwFoSps0jegECgYEA7o5AcRTZVUmmSs8W\n"
        "PUHn89UEuDAMFVk7grG1bg8exLQSpugCykcqXt1WNrqB7x6nB+dbVANWNhSmhgCg\n"
        "VrV941vbx8ketqZ9YInSbGPWIU/tss3r8Yx2Ct3mQpvpGC6iGHzEc/NHJP8Efvh/\n"
        "CcUWmLjLGJYYeP5oNu5cncC3fXUCgYEA2LANATm0A6sFVGe3sSLO9un1brA4zlZE\n"
        "Hjd3KOZnMPt73B426qUOcw5B2wIS8GJsUES0P94pKg83oyzmoUV9vJpJLjHA4qmL\n"
        "CDAd6CjAmE5ea4dFdZwDDS8F9FntJMdPQJA9vq+JaeS+k7ds3+7oiNe+RUIHR1Sz\n"
        "VEAKh3Xw66kCgYB7KO/2Mchesu5qku2tZJhHF4QfP5cNcos511uO3bmJ3ln+16uR\n"
        "GRqz7Vu0V6f7dvzPJM/O2QYqV5D9f9dHzN2YgvU9+QSlUeFK9PyxPv3vJt/WP1//\n"
        "zf+nbpaRbwLxnCnNsKSQJFpnrE166/pSZfFbmZQpNlyeIuJU8czZGQTifQKBgHXe\n"
        "/pQGEZhVNab+bHwdFTxXdDzr+1qyrodJYLaM7uFES9InVXQ6qSuJO+WosSi2QXlA\n"
        "hlSfwwCwGnHXAPYFWSp5Owm34tbpp0mi8wHQ+UNgjhgsE2qwnTBUvgZ3zHpPORtD\n"
        "23KZBkTmO40bIEyIJ1IZGdWO32q79nkEBTY+v/lRAoGBAI1rbouFYPBrTYQ9kcjt\n"
        "1yfu4JF5MvO9JrHQ9tOwkqDmNCWx9xWXbgydsn/eFtuUMULWsG3lNjfst/Esb8ch\n"
        "k5cZd6pdJZa4/vhEwrYYSuEjMCnRb0lUsm7TsHxQrUd6Fi/mUuFU/haC0o0chLq7\n"
        "pVOUFq5mW8p0zbtfHbjkgxyF\n"
        "-----END PRIVATE KEY-----\n";

    std::string const dh =
        "-----BEGIN DH PARAMETERS-----\n"
        "MIIBCAKCAQEArzQc5mpm0Fs8yahDeySj31JZlwEphUdZ9StM2D8+Fo7TMduGtSi+\n"
        "/HRWVwHcTFAgrxVdm+dl474mOUqqaz4MpzIb6+6OVfWHbQJmXPepZKyu4LgUPvY/\n"
        "4q3/iDMjIS0fLOu/bLuObwU5ccZmDgfhmz1GanRlTQOiYRty3FiOATWZBRh6uv4u\n"
        "tff4A9Bm3V9tLx9S6djq31w31Gl7OQhryodW28kc16t9TvO1BzcV3HjRPwpe701X\n"
        "oEEZdnZWANkkpR/m/pfgdmGPU66S2sXMHgsliViQWpDCYeehrvFRHEdR9NV+XJfC\n"
        "QMUk26jPTIVTLfXmmwU0u8vUkpR7LQKkwwIBAg==\n"
        "-----END DH PARAMETERS-----\n";

    ctx.set_options(
        boost::asio::ssl::context::verify_none |
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::single_dh_use);

    ctx.use_certificate_chain(
        boost::asio::buffer(cert.data(), cert.size()));

    ctx.use_private_key(
        boost::asio::buffer(key.data(), key.size()),
        boost::asio::ssl::context::file_format::pem);

    ctx.use_tmp_dh(
        boost::asio::buffer(dh.data(), dh.size()));
  }

  TEST_CASE("Create https server and client, do some https communications", "[https]")
  {
    auto as = asyik::make_service();

    // The SSL context is required, and holds certificates
    ssl::context ctx{ssl::context::tlsv12};

    // This holds the self-signed certificate used by the server
    load_server_certificate(ctx);

    auto server = asyik::make_https_server(as, std::move(ctx), "127.0.0.1", 4004);

    server->on_websocket("/ws", [](auto ws, auto args) {
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

    asyik::sleep_for(std::chrono::milliseconds(100));
    as->execute([as]() {
      auto req = asyik::http_easy_request(as, "POST", "https://127.0.0.1:4004/post-only/30/name/listed", "hehe", {{"x-test", "ok"}});

      REQUIRE(req->response.result() == 200);
      std::string x_test_reply{req->response.headers["x-test-reply"]};
      std::string body = req->response.body;
      REQUIRE(!body.compare("hehe-30-listed-ok"));
      REQUIRE(!x_test_reply.compare("amiiin"));

      req = asyik::http_easy_request(as, "GET", "https://127.0.0.1:4004/not-found");
      REQUIRE(req->response.result() == 404);

      asyik::websocket_ptr ws = asyik::make_websocket_connection(as, "wss://127.0.0.1:4004/ws");

      ws->send_string("halo");
      auto s = ws->get_string();
      REQUIRE(!s.compare("halo"));

      ws->close(websocket_close_code::normal, "closed normally");

      as->stop();
    });

    as->run();
  }

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
                std::make_exception_ptr(asyik::network_error("send error")));
        });
    return std::move(future);
  }

  TEST_CASE("test manual handling of http requests", "[http]")
  {
    auto as = asyik::make_service();

    // The SSL context is required, and holds certificates
    ssl::context ctx{ssl::context::tlsv12};

    // This holds the self-signed certificate used by the server
    load_server_certificate(ctx);

    auto server = asyik::make_http_server(as, "127.0.0.1", 4004);
    auto server2 = asyik::make_https_server(as, std::move(ctx), "127.0.0.1", 4005);

    server->on_http_request("/manual", "GET", [server](auto req, auto args) {
      auto connection = req->get_connection_handle(server);
      auto &stream = connection->get_stream();
      req->activate_direct_response_handling();

      std::string body{req->headers["x-test"]};
      std::string s = "HTTP/1.1 200 OK\r\n"
                      "Content-type: text/plain\r\n"
                      "X-Test-Reply: amiiin\r\n"
                      "Content-length: " +
                      std::to_string(body.length()) + "\r\n\r\n";

      async_send(stream, asio::buffer(s.data(), s.length())).get();
      async_send(stream, asio::buffer(body.data(), body.length())).get();
    });

    server2->on_http_request("/manual", "GET", [server2](auto req, auto args) {
      auto connection = req->get_connection_handle(server2);
      auto &stream = connection->get_stream();
      req->activate_direct_response_handling();

      std::string body{req->headers["x-test"]};
      std::string s = "HTTP/1.1 200 OK\r\n"
                      "Content-type: text/plain\r\n"
                      "X-Test-Reply: amiiin\r\n"
                      "Content-length: " +
                      std::to_string(body.length()) + "\r\n\r\n";

      async_send(stream, asio::buffer(s.data(), s.length())).get();
      async_send(stream, asio::buffer(body.data(), body.length())).get();
    });

    asyik::sleep_for(std::chrono::milliseconds(100));
    as->execute([as]() {
      {
        auto req = asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/manual", "", {{"x-test", "ok"}});

        REQUIRE(req->response.result() == 200);
        std::string x_test_reply{req->response.headers["x-test-reply"]};
        std::string body = req->response.body;
        REQUIRE(!body.compare("ok"));
        REQUIRE(!x_test_reply.compare("amiiin"));
      }

      {
        auto req = asyik::http_easy_request(as, "GET", "https://127.0.0.1:4005/manual", "", {{"x-test", "ok"}});

        REQUIRE(req->response.result() == 200);
        std::string x_test_reply{req->response.headers["x-test-reply"]};
        std::string body = req->response.body;
        REQUIRE(!body.compare("ok"));
        REQUIRE(!x_test_reply.compare("amiiin"));
      }

      as->stop();
    });

    as->run();
  }
} // namespace asyik