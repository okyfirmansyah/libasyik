#include "catch2/catch.hpp"
#include "libasyik/http.hpp"
#include "libasyik/route_table.hpp"
#include "libasyik/service.hpp"

namespace asyik {
void _TEST_invoke_http(){};
inline void load_server_certificate(boost::asio::ssl::context& ctx)
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

  ctx.set_options(boost::asio::ssl::context::verify_none |
                  boost::asio::ssl::context::default_workarounds |
                  boost::asio::ssl::context::no_sslv2 |
                  boost::asio::ssl::context::single_dh_use);

  ctx.use_certificate_chain(boost::asio::buffer(cert.data(), cert.size()));

  ctx.use_private_key(boost::asio::buffer(key.data(), key.size()),
                      boost::asio::ssl::context::file_format::pem);

  ctx.use_tmp_dh(boost::asio::buffer(dh.data(), dh.size()));
}

TEST_CASE("check routing spec to regex spec conversion")
{
  std::string spec = "/api/v1/face-match";
  REQUIRE(!internal::route_spec_to_regex(spec).compare(
      R"(^\/api\/v1\/face\-match\/?(|\?[^\?\s]*)$)"));

  spec = "/api/v1/face-match/";
  REQUIRE(!internal::route_spec_to_regex(spec).compare(
      R"(^\/api\/v1\/face\-match\/?(|\?[^\?\s]*)$)"));

  spec = "/api/v1/face-match/<int>/";
  REQUIRE(!internal::route_spec_to_regex(spec).compare(
      R"(^\/api\/v1\/face\-match\/([0-9]+)\/?(|\?[^\?\s]*)$)"));

  spec = "/api/v1/face-match/<string>/n/< int  >";
  REQUIRE(!internal::route_spec_to_regex(spec).compare(
      R"(^\/api\/v1\/face\-match\/([^\/\?\s]+)\/n\/([0-9]+)\/?(|\?[^\?\s]*)$)"));
}

TEST_CASE("check http_analyze_url against URL cases")
{
  bool result;
  http_url_scheme scheme;

  result = http_analyze_url("http://simple.com", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("simple.com"));
  REQUIRE(!scheme.target().length());
  REQUIRE(scheme.port() == 80);
  REQUIRE(!scheme.is_ssl());

  result = http_analyze_url("Http://protspecify.co.id", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("protspecify.co.id"));
  REQUIRE(!scheme.target().length());
  REQUIRE(scheme.port() == 80);
  REQUIRE(!scheme.is_ssl());

  result = http_analyze_url("https://prothttps.x.y.z/", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("prothttps.x.y.z"));
  REQUIRE(!scheme.target().compare("/"));
  REQUIRE(scheme.port() == 443);
  REQUIRE(scheme.is_ssl());

  result = http_analyze_url("wss://prothttps.x.y.z", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("prothttps.x.y.z"));
  REQUIRE(!scheme.target().length());
  REQUIRE(scheme.port() == 443);
  REQUIRE(scheme.is_ssl());

  result = http_analyze_url("https://protandport.x:69", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("protandport.x"));
  REQUIRE(!scheme.target().length());
  REQUIRE(scheme.port() == 69);
  REQUIRE(scheme.is_ssl());

  result = http_analyze_url("http://simple.x.y.z:443/check", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("simple.x.y.z"));
  REQUIRE(!scheme.target().compare("/check"));
  REQUIRE(scheme.port() == 443);
  REQUIRE(!scheme.is_ssl());

  result = http_analyze_url("http://simple.co.id:443/check", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("simple.co.id"));
  REQUIRE(!scheme.target().compare("/check"));
  REQUIRE(scheme.port() == 443);
  REQUIRE(!scheme.is_ssl());

  result = http_analyze_url("http://10.10.10.1:443/check", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("10.10.10.1"));
  REQUIRE(!scheme.target().compare("/check"));
  REQUIRE(scheme.port() == 443);
  REQUIRE(!scheme.is_ssl());

  result = http_analyze_url("http://10.10.10.1/check?hehe", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("10.10.10.1"));
  REQUIRE(!scheme.target().compare("/check?hehe"));

  result = http_analyze_url("http://10.10.10.1/check/?result=ok", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("10.10.10.1"));
  REQUIRE(!scheme.target().compare("/check/?result=ok"));

  result = http_analyze_url(
      "http://10.10.10.1/check/"
      "?result=ok&msg=nothing+important&msg2=ok%20done#page1",
      scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("10.10.10.1"));
  REQUIRE(!scheme.target().compare(
      "/check/?result=ok&msg=nothing+important&msg2=ok%20done#page1"));

  result = http_analyze_url(
      "http://10.10.10.1/check/?result=ok&msg=nothing+important&msg2=ok done",
      scheme);
  REQUIRE(!result);

  result = http_analyze_url("http://he he.com/check/", scheme);
  REQUIRE(!result);

  result = http_analyze_url("http://he he.com:232/check/", scheme);
  REQUIRE(!result);

  result = http_analyze_url("http://10.10.10.1:443/", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("10.10.10.1"));
  REQUIRE(!scheme.target().compare("/"));
  REQUIRE(!scheme.username().length());
  REQUIRE(!scheme.password().length());
  REQUIRE(scheme.port() == 443);
  REQUIRE(!scheme.is_ssl());

  result = http_analyze_url("http://test@10.10.10.1:443/", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("10.10.10.1"));
  REQUIRE(!scheme.authority().compare("test@10.10.10.1:443"));
  REQUIRE(!scheme.username().compare("test"));
  REQUIRE(!scheme.password().length());

  result = http_analyze_url("http://test:pwd%21001@10.10.10.1:443/", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("10.10.10.1"));
  REQUIRE(!scheme.authority().compare("test:pwd%21001@10.10.10.1:443"));
  REQUIRE(!scheme.username().compare("test"));
  REQUIRE(!scheme.password().compare("pwd!001"));

  result = http_analyze_url("http://10.10.10.2", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.host().compare("10.10.10.2"));
  REQUIRE(!scheme.target().length());
  REQUIRE(scheme.port() == 80);
  REQUIRE(!scheme.is_ssl());

  result = http_analyze_url("invalid://simple.com:443/check", scheme);
  REQUIRE(result);
  REQUIRE(!scheme.get_url_view().scheme().compare("invalid"));
  REQUIRE(scheme.get_url_view().scheme_id() == boost::urls::scheme::unknown);

  result = http_analyze_url("invalid:simple.com:443/check", scheme);
  REQUIRE(!result);

  result = http_analyze_url("simple.com:443:323/check", scheme);
  REQUIRE(!result);
}

TEST_CASE("Create http server and client, do some websocket communications",
          "[websocket]")
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

#if __cplusplus >= 201402L
  server->on_websocket("/<int>/name/<string>", [as, server](auto ws,
                                                            auto args) {
#else
  server->on_websocket("/<int>/name/<string>", [as, server](
                                                   websocket_ptr ws,
                                                   const http_route_args&
                                                       args) {
#endif
    REQUIRE(as == asyik::get_current_service());
    auto ep = ws->request->get_connection_handle(server)->get_remote_endpoint();
    LOG(INFO) << "new websocket connection from " << ep.address().to_string()
              << "\n";

    ws->set_idle_timeout(2);
    ws->set_keepalive_pings(true);

    while (1) {
      auto s = ws->get_string();
      ws->send_string(args[1] + "." + s + "-" + args[2]);
    }
  });

  int success = 0;
  for (int i = 0; i < 8; i++)
    as->execute([=, &success]() {
      asyik::sleep_for(std::chrono::milliseconds(50 * i));

      asyik::websocket_ptr ws = asyik::make_websocket_connection(
          as, "ws://127.0.0.1:4004/" + std::to_string(i) + "/name/hash");

      // this to check if keep alive in server side save us from being timeout'd
      as->execute([=]() {
        asyik::sleep_for(std::chrono::milliseconds(2500));
        ws->send_string("halo");
      });

      auto s = ws->get_string();
      if (!s.compare(std::to_string(i) + ".halo-hash")) success++;

      ws->send_string("there");
      s = ws->get_string();
      if (!s.compare(std::to_string(i) + ".there-hash")) success++;

      ws->close(websocket_close_code::normal, "closed normally");
    });

  // check SSL websocket client(using external WSS server)
  as->execute([=, &success]() {
    asyik::sleep_for(std::chrono::milliseconds(50));

    // TODO: temporarily disabled because the public echo server is unavailable
    // asyik::websocket_ptr ws = asyik::make_websocket_connection(as,
    // "wss://echo.websocket.org");

    // ws->set_keepalive_pings(true);
    // ws->set_idle_timeout(5);
    // ws->send_string("halo");
    // auto s = ws->get_string();
    // if (!s.compare("halo"))
    //   success++;

    // ws->send_string("there");
    // s = ws->get_string();
    // if (!s.compare("there"))
    //   success++;

    // ws->close(websocket_close_code::normal, "closed normally");
    success += 2;
  });

  // check ws client timeout
  as->execute([=, &success]() {
    asyik::sleep_for(std::chrono::milliseconds(50));

    asyik::websocket_ptr ws = asyik::make_websocket_connection(
        as, "ws://justtest:nvm@127.0.0.1:4004/0/name/hash");

    ws->set_idle_timeout(1);
    try {
      auto s = ws->get_string();
      REQUIRE(false);
    } catch (const std::exception& e) {
      LOG(INFO) << "websocket idle timeout correctly received: " << e.what()
                << "\n";
      REQUIRE(true);
      success++;
    }
  });

  // check ws keepalive mechanism(should be timeout'd if keep alive is not
  // processed)
  as->execute([=, &success]() {
    asyik::sleep_for(std::chrono::milliseconds(50));

    asyik::websocket_ptr ws =
        asyik::make_websocket_connection(as, "ws://127.0.0.1:4004/0/name/hash");
    ws->set_idle_timeout(1);
    ws->set_keepalive_pings(true);
    as->execute([=]() {
      asyik::sleep_for(std::chrono::milliseconds(1500));
      ws->send_string("hello");
    });
    auto s = ws->get_string();
    LOG(INFO) << "keep alive ping keep ws connection open\n";
    REQUIRE(!s.compare("0.hello-hash"));
    success++;
  });

  // task to timeout entire service runtime
  as->execute([=, &success]() {
    for (int i = 0; i < 200; i++)  // timeout 20s
    {
      if (success >= 20) break;
      asyik::sleep_for(std::chrono::milliseconds(100));
    }
    server->close();
    as->stop();
  });

  as->run();
  LOG(INFO) << "selesai!\n";
  REQUIRE(success == 20);
}

TEST_CASE("Create http server and client, do some http communications",
          "[http]")
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

  server->on_websocket("/<int>/test/<string>", [](websocket_ptr ws,
                                                  const http_route_args& args) {
    ws->send_string(args[1]);
    ws->send_string(args[2]);

    auto s = ws->get_string();
    ws->send_string(s);

    ws->close(websocket_close_code::normal, "closed normally");
  });

  server->on_http_request(
      "/post-only/<int>/name/<string>", "POST",
#if __cplusplus >= 201402L
      [as](auto req, auto args) {
#else
      [as](http_request_ptr req, const http_route_args& args) {
#endif
        REQUIRE(as == asyik::get_current_service());
        req->response.headers.set("x-test-reply", "amiiin");
        req->response.headers.set("content-type", "text/json");
        std::string x_test{req->headers["x-test"]};
        req->response.body =
            req->body + "-" + args[1] + "-" + args[2] + "-" + x_test;
        req->response.result(200);
      });

  server->on_http_request(
      "/any/<int>/", [as](http_request_ptr req, const http_route_args& args) {
        REQUIRE(as == asyik::get_current_service());
        req->response.headers.set("x-test-reply", "amiin");
        req->response.headers.set("content-type", "text/json");
        std::string s{req->method()};
        std::string x_test{req->headers["x-test"]};
        req->response.body = req->body + "-" + s + "-" + args[1] + "-" + x_test;
        req->response.result(200);
      });

  server->on_http_request(
      "/big_size/", [as](http_request_ptr req, const http_route_args& args) {
        REQUIRE(as == asyik::get_current_service());
        REQUIRE(req->body.size() ==
                std::stoull(std::string(req->headers["Content-Length"])));
        LOG(INFO) << "HTTP payload with big size received successfully\n";
        req->response.result(200);
      });

  server->set_request_body_limit(20 * 1024 * 1024);
  server->set_request_header_limit(1024);

  asyik::sleep_for(std::chrono::milliseconds(100));
  as->execute([as, server]() {
    auto req = asyik::http_easy_request(
        as, "POST", "http://127.0.0.1:4004/post-only/30/name/listed?dummy=hihi",
        "hehe", {{"x-test", "ok"}});

    REQUIRE(req->response.result() == 200);
    std::string x_test_reply{req->response.headers["x-test-reply"]};
    std::string body = req->response.body;
    REQUIRE(!body.compare("hehe-30-listed-ok"));
    REQUIRE(!x_test_reply.compare("amiiin"));

    req = asyik::http_easy_request(
        as, "GET",
        "http://nvm_user:nvm_pwd@127.0.0.1:4004/any/999/?dummy1&dummy2=ha+ha");

    REQUIRE(req->response.result() == 200);
    REQUIRE(!req->response.body.compare("-GET-999-"));
    REQUIRE(!req->response.headers["x-test-reply"].compare("amiin"));

    req = asyik::http_easy_request(
        as, "GET", "http://127.0.0.1:4004/any/123?dummy=ha%20ha", "",
        {{"x-test", "sip"}});

    REQUIRE(req->response.result() == 200);
    REQUIRE(!req->response.body.compare("-GET-123-sip"));
    REQUIRE(!req->response.headers["x-test-reply"].compare("amiin"));

    req =
        asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/not-found");
    REQUIRE(req->response.result() == 404);

    std::string binary;
    binary.resize(16 * 1024 * 1024);
    memset(&binary[0], ' ', binary.size());
    try {
      req = asyik::http_easy_request(
          as, "GET", "http://127.0.0.1:4004/big_size", binary, {});
      REQUIRE(req->response.result() == 200);
    } catch (std::exception& e) {
      LOG(ERROR) << "test big size payload failed. What: " << e.what() << "\n";
      REQUIRE(false);
    }

    // negative tests (TODO)
    // req = asyik::http_easy_request(as, "GET",
    // "3878sad9das7d97d8safdsfd.com/not-found");
    // REQUIRE(req->response.result()==404);

    // SSL Test
    req = asyik::http_easy_request(as, "GET",
                                   "https://tls-v1-2.badssl.com:1012/");
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body.length());

    req = asyik::http_easy_request(as, "GET",
                                   "https://tls-v1-0.badssl.com:1012/");
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body.length());

    // SSL Negative tests (TODO)

    // body limit negative case
    binary.resize(24 * 1024 * 1024);
    memset(&binary[0], ' ', binary.size());
    try {
      req = asyik::http_easy_request(
          as, "GET", "http://127.0.0.1:4004/big_size", binary, {});
      REQUIRE(req->response.result() == 413);
      LOG(INFO) << "Got correct HTTP 413: " << req->response.body << "\n";
    } catch (std::exception& e) {
      LOG(ERROR)
          << "Unexpected exception for oversize http request body. What: "
          << e.what() << "\n";
      REQUIRE(false);
    }

    // header limit negative case
    binary.resize(2 * 1024);
    memset(&binary[0], 'A', binary.size());
    try {
      req =
          asyik::http_easy_request(as, "GET", "http://127.0.0.1:4004/big_size",
                                   " ", {{"x-big", binary}});
      REQUIRE(req->response.result() == 413);
      LOG(INFO) << "Got correct HTTP 413: " << req->response.body << "\n";
    } catch (std::exception& e) {
      LOG(ERROR)
          << "Unexpected exception for oversize http request header. What: "
          << e.what() << "\n";
      REQUIRE(false);
    }
    server->close();
    as->stop();
  });

  // auto client = asyik::make_http_connection(as, "127.0.0.1", "80");
  // auto req=std::make_shared<http_request>();
  // client->send_request(req);

  as->run();
}

TEST_CASE("Test http client timeout", "[http]")
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4007);

  server->on_http_request(
      "/timeout", [as](http_request_ptr req, const http_route_args& args) {
        asyik::sleep_for(std::chrono::seconds(5));

        REQUIRE(as == asyik::get_current_service());
        req->response.body = "ok";
        req->response.result(200);
      });

  asyik::sleep_for(std::chrono::milliseconds(100));
  as->execute([as, server]() {
    LOG(INFO) << "begin accessing long query..\n";
    try {
      auto req = asyik::http_easy_request(
          as, 3000, "GET", "http://127.0.0.1:4007/timeout", "", {});

      REQUIRE(false);  // if we reach here, somethins is wrong
    } catch (asyik::network_timeout_error& e) {
      LOG(INFO) << "timeout exception catched.\n";
      REQUIRE(true);
    } catch (std::exception& e) {
      LOG(ERROR) << "Caught unexpected exception: " << e.what() << "\n";
      REQUIRE(false);
    }
    asyik::sleep_for(std::chrono::seconds(3));
    server->close();
    as->stop();
  });

  as->run();
}

#ifdef LIBASYIK_ENABLE_SSL_SERVER
TEST_CASE("Create https server and client, do some https communications",
          "[https]")
{
  auto as = asyik::make_service();

  // The SSL context is required, and holds certificates
  ssl::context ctx{ssl::context::tlsv12};

  // This holds the self-signed certificate used by the server
  load_server_certificate(ctx);

  auto server = asyik::make_https_server(as, std::move(ctx), "127.0.0.1", 4004);

  server->on_websocket(
      "/ws", [](websocket_ptr ws, const http_route_args& args) {
        auto s = ws->get_string();
        ws->send_string(s);

        ws->close(websocket_close_code::normal, "closed normally");
      });

  server->on_http_request(
      "/post-only/<int>/name/<string>", "POST",
      [](http_request_ptr req, const http_route_args& args) {
        req->response.headers.set("x-test-reply", "amiiin");
        req->response.headers.set("content-type", "text/json");

        std::string x_test{req->headers["x-test"]};
        req->response.body =
            req->body + "-" + args[1] + "-" + args[2] + "-" + x_test;
        req->response.result(200);
      });

  server->on_http_request(
      "/timeout", [](http_request_ptr req, const http_route_args& args) {
        asyik::sleep_for(std::chrono::seconds(5));

        req->response.body = "ok";
        req->response.result(200);
      });

  server->on_http_request(
      "/big_size/", [](http_request_ptr req, const http_route_args& args) {
        REQUIRE(req->body.size() ==
                std::stoull(std::string(req->headers["Content-Length"])));
        LOG(INFO) << "HTTP payload with big size received successfully\n";
        req->response.result(200);
      });

  server->set_request_body_limit(20 * 1024 * 1024);
  server->set_request_header_limit(1024);

  asyik::sleep_for(std::chrono::milliseconds(100));
  as->execute([as, server]() {
    auto req = asyik::http_easy_request(
        as, "POST", "https://127.0.0.1:4004/post-only/30/name/listed", "hehe",
        {{"x-test", "ok"}});

    REQUIRE(req->response.result() == 200);
    std::string x_test_reply{req->response.headers["x-test-reply"]};
    std::string body = req->response.body;
    REQUIRE(!body.compare("hehe-30-listed-ok"));
    REQUIRE(!x_test_reply.compare("amiiin"));

    req =
        asyik::http_easy_request(as, "GET", "https://127.0.0.1:4004/not-found");
    REQUIRE(req->response.result() == 404);

    asyik::websocket_ptr ws =
        asyik::make_websocket_connection(as, "wss://127.0.0.1:4004/ws");

    ws->send_string("halo");
    auto s = ws->get_string();
    REQUIRE(!s.compare("halo"));

    ws->close(websocket_close_code::normal, "closed normally");

    LOG(INFO) << "begin accessing long query(SSL)..\n";
    try {
      auto req = asyik::http_easy_request(
          as, 3000, "GET", "https://127.0.0.1:4004/timeout", "", {});

      REQUIRE(false);  // if we reach here, somethins is wrong
    } catch (asyik::network_timeout_error& e) {
      LOG(INFO) << "timeout exception catched.\n";
      REQUIRE(true);
    } catch (std::exception& e) {
      LOG(ERROR) << "Caught unexpected exception(SSL): " << e.what() << "\n";
      REQUIRE(false);
    }
    asyik::sleep_for(std::chrono::seconds(3));

    // Test SSL Big size
    std::string binary;
    binary.resize(16 * 1024 * 1024);
    memset(&binary[0], ' ', binary.size());
    try {
      req = asyik::http_easy_request(
          as, "GET", "https://127.0.0.1:4004/big_size", binary, {});
      REQUIRE(req->response.result() == 200);
    } catch (std::exception& e) {
      LOG(ERROR) << "test big size(SSL) payload failed. What: " << e.what()
                 << "\n";
      REQUIRE(false);
    }

    // body limit negative case
    binary.resize(24 * 1024 * 1024);
    memset(&binary[0], ' ', binary.size());
    try {
      req = asyik::http_easy_request(
          as, "GET", "https://127.0.0.1:4004/big_size", binary, {});
      REQUIRE(req->response.result() == 413);
      LOG(INFO) << "(SSL oversize test)Got correct HTTP 413: "
                << req->response.body << "\n";
    } catch (std::exception& e) {
      LOG(ERROR)
          << "Unexpected exception for oversize http(SSL) request body. What: "
          << e.what() << "\n";
      REQUIRE(false);
    }

    // header limit negative case
    binary.resize(2 * 1024);
    memset(&binary[0], 'A', binary.size());
    try {
      req =
          asyik::http_easy_request(as, "GET", "https://127.0.0.1:4004/big_size",
                                   " ", {{"x-big", binary}});
      REQUIRE(req->response.result() == 413);
      LOG(INFO) << "(SSL oversize test)Got correct HTTP 413: "
                << req->response.body << "\n";
    } catch (std::exception& e) {
      LOG(ERROR) << "Unexpected exception for oversize http(SSL) request "
                    "header. What: "
                 << e.what() << "\n";
      REQUIRE(false);
    }
    server->close();
    as->stop();
  });

  as->run();
}
#endif  // LIBASYIK_ENABLE_SSL_SERVER

TEST_CASE("test manual handling of http requests", "[http]")
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4004);
#ifdef LIBASYIK_ENABLE_SSL_SERVER
  // The SSL context is required, and holds certificates
  ssl::context ctx{ssl::context::tlsv12};

  // This holds the self-signed certificate used by the server
  load_server_certificate(ctx);

  auto server2 =
      asyik::make_https_server(as, std::move(ctx), "127.0.0.1", 4008);
#endif

  server->on_http_request(
      "/manual", "GET",
      [server](http_request_ptr req, const http_route_args& args) {
        auto connection = req->get_connection_handle(server);
        auto& stream = connection->get_stream();
        req->activate_direct_response_handling();

        std::string body{req->headers["x-test"]};
        std::string s =
            "HTTP/1.1 200 OK\r\n"
            "Content-type: text/plain\r\n"
            "X-Test-Reply: amiiin\r\n"
            "Content-length: " +
            std::to_string(body.length()) + "\r\n\r\n";

        stream
            .async_write_some(asio::buffer(s.data(), s.length()),
                              use_fiber_future)
            .get();
        stream
            .async_write_some(asio::buffer(body.data(), body.length()),
                              use_fiber_future)
            .get();
      });

#ifdef LIBASYIK_ENABLE_SSL_SERVER
  server2->on_http_request(
      "/manual", "GET",
      [server2](http_request_ptr req, const http_route_args& args) {
        auto connection = req->get_connection_handle(server2);
        auto& stream = connection->get_stream();
        req->activate_direct_response_handling();

        std::string body{req->headers["x-test"]};
        std::string s =
            "HTTP/1.1 200 OK\r\n"
            "Content-type: text/plain\r\n"
            "X-Test-Reply: amiiin\r\n"
            "Content-length: " +
            std::to_string(body.length()) + "\r\n\r\n";

        asyik::sleep_for(std::chrono::milliseconds(50));

        stream
            .async_write_some(asio::buffer(s.data(), s.length()),
                              use_fiber_future)
            .get();
        stream
            .async_write_some(asio::buffer(body.data(), body.length()),
                              use_fiber_future)
            .get();
      });
#endif

  asyik::sleep_for(std::chrono::milliseconds(100));
  as->execute([as, server
#ifdef LIBASYIK_ENABLE_SSL_SERVER
               ,
               server2
#endif
  ]() {
    {
      auto req = asyik::http_easy_request(
          as, "GET", "http://127.0.0.1:4004/manual", "", {{"x-test", "ok"}});

      REQUIRE(req->response.result() == 200);
      std::string x_test_reply{req->response.headers["x-test-reply"]};
      std::string body = req->response.body;
      REQUIRE(!body.compare("ok"));
      REQUIRE(!x_test_reply.compare("amiiin"));
    }

    LOG(INFO) << "performing client requests from inside async()..\n";
#ifdef LIBASYIK_ENABLE_SSL_SERVER
    std::atomic<int> counter{0};
    for (int i = 0; i < 50; i++) {
      as->async([as, &counter]() {
        for (int j = 0; j < 10; j++) {
          as->async([as, &counter]() {
              auto req = asyik::http_easy_request(
                  as, "GET", "https://127.0.0.1:4008/manual", "",
                  {{"x-test", "ok"}});

              REQUIRE(req->response.result() == 200);
              std::string x_test_reply{req->response.headers["x-test-reply"]};
              std::string body = req->response.body;
              REQUIRE(!body.compare("ok"));
              REQUIRE(!x_test_reply.compare("amiiin"));
              counter++;
            }).get();
          asyik::sleep_for(std::chrono::milliseconds(1));
        }
      });
      asyik::sleep_for(std::chrono::milliseconds(10));
    }

    while (counter < 500) asyik::sleep_for(std::chrono::milliseconds(50));

    LOG(INFO) << "performing client requests from inside async() done.\n";
#endif

    // test what if, we dont close the server properly
    // server->close();
    // server2->close();
    as->stop();
  });

  as->run();
}

TEST_CASE("Test for multithread server(SO_REUSEPORT)", "[http]")
{
  std::atomic<bool> stopped;
  stopped = false;

  for (int i = 0; i < 8; i++) {
    std::thread t([&stopped]() {
      auto as = asyik::make_service();
      LOG(INFO) << "tried to multi-bind(http)..\n";
      auto server = asyik::make_http_server(as, "127.0.0.1", 4009, true);
      LOG(INFO) << "multi-bind(http) success\n";

#ifdef LIBASYIK_ENABLE_SSL_SERVER
      // This holds the self-signed certificate used by the server
      ssl::context ctx{ssl::context::tlsv12};
      load_server_certificate(ctx);

      LOG(INFO) << "tried to multi-bind(https)..\n";
      auto server2 =
          asyik::make_https_server(as, std::move(ctx), "127.0.0.1", 4010, true);
      LOG(INFO) << "multi-bind(https) success\n";
#endif

      bool flag_http = false;
#ifdef LIBASYIK_ENABLE_SSL_SERVER
      bool flag_https = false;
#endif

      server->on_http_request("/flag", "GET",
#if __cplusplus >= 201402L
                              [&flag_http](auto req, auto args) {
#else
        [&flag_http](http_request_ptr req, const http_route_args& args) {
#endif
                                flag_http = true;
                                req->response.body = "flagging ok";
                                req->response.result(200);
                              });

#ifdef LIBASYIK_ENABLE_SSL_SERVER
      server2->on_http_request("/flag", "GET",
#if __cplusplus >= 201402L
                               [&flag_https](auto req, auto args) {
#else
        [&flag_https](http_request_ptr req, const http_route_args& args) {
#endif
                                 flag_https = true;
                                 req->response.body = "flagging ok";
                                 req->response.result(200);
                               });
#endif

      as->execute([&stopped, as]() {
        while (!stopped) asyik::sleep_for(std::chrono::milliseconds(10));
        as->stop();
      });

      as->run();
      REQUIRE(flag_http);
#ifdef LIBASYIK_ENABLE_SSL_SERVER
      REQUIRE(flag_https);
#endif
    }  // namespace asyik
    );
    t.detach();
  }

  auto as = asyik::make_service();
  as->execute([&stopped, as]() {
    for (int i = 0; i < 100; i++) {
      auto req =
          asyik::http_easy_request(as, "GET", "http://127.0.0.1:4009/flag");
      REQUIRE(req->response.result() == 200);
#ifdef LIBASYIK_ENABLE_SSL_SERVER
      auto req2 =
          asyik::http_easy_request(as, "GET", "https://127.0.0.1:4010/flag");
      REQUIRE(req2->response.result() == 200);
#endif
    }
    stopped = true;
    as->stop();
  });
  as->run();
  LOG(INFO) << "testing multithread http server done\n";
  asyik::sleep_for(std::chrono::milliseconds(500));
}

TEST_CASE("Test for multipart", "[http]")
{
  auto as = asyik::make_service();
  auto server = asyik::make_http_server(as, "127.0.0.1", 4011, true);

#ifdef LIBASYIK_ENABLE_SSL_SERVER
  // This holds the self-signed certificate used by the server
  ssl::context ctx{ssl::context::tlsv12};
  load_server_certificate(ctx);

  auto server2 =
      asyik::make_https_server(as, std::move(ctx), "127.0.0.1", 4012, true);
#endif

  server->on_http_request(
      "/multipart", "GET",
#if __cplusplus >= 201402L
      [server](auto req, auto args) {
#else
      [server](http_request_ptr req, const http_route_args& args) {
#endif
        auto connection = req->get_connection_handle(server);
        auto& stream = connection->get_stream();
        req->activate_direct_response_handling();

        std::string body1{"100"};
        std::string body2{"200"};
        std::string body3{"300"};

        std::string s =
            "HTTP/1.1 200 OK\r\n"
            "Connection: keep-alive\r\n"
            "Cache-Control: no-cache\r\n"
            "Content-Type: "
            "multipart/x-mixed-replace;boundary=--boundaryLibAsyik00001\r\n"
            "X-Test-Reply: amiiin\r\n"
            "\r\n"
            "--boundaryLibAsyik00001\r\n"
            "x-mpart-id: 1\r\n"
            "Content-length: " +
            std::to_string(body1.length()) +
            "\r\n"
            "Content-type: image/jpeg\r\n"
            "\r\n" +
            body1 +
            "--boundaryLibAsyik00001\r\n"
            "x-mpart-id: 2\r\n"
            "Content-length: " +
            std::to_string(body2.length()) +
            "\r\n"
            "Content-type: image/jpeg\r\n"
            "\r\n" +
            body2 +
            "--boundaryLibAsyik00001\r\n"
            "x-mpart-id: 3\r\n"
            "Content-length: " +
            std::to_string(body3.length()) +
            "\r\n"
            "Content-type: image/jpeg\r\n"
            "\r\n" +
            body3 + "--boundaryLibAsyik00001--\r\n";

        asio::async_write(stream, asio::buffer(s.data(), s.length()),
                          use_fiber_future)
            .get();
      });

#ifdef LIBASYIK_ENABLE_SSL_SERVER
  server2->on_http_request(
      "/multipart", "GET",
#if __cplusplus >= 201402L
      [server2](auto req, auto args) {
#else
      [server2](http_request_ptr req, const http_route_args& args) {
#endif
        auto connection = req->get_connection_handle(server2);
        auto& stream = connection->get_stream();
        req->activate_direct_response_handling();

        std::string body1{"100"};
        std::string body2{"200"};
        std::string body3{"300"};

        std::string s =
            "HTTP/1.1 200 OK\r\n"
            "Connection: keep-alive\r\n"
            "Cache-Control: no-cache\r\n"
            "Content-Type: "
            "multipart/x-mixed-replace;boundary=--boundaryLibAsyik00001\r\n"
            "X-Test-Reply: amiiin\r\n"
            "\r\n"
            "--boundaryLibAsyik00001\r\n"
            "x-mpart-id: 1\r\n"
            "Content-length: " +
            std::to_string(body1.length()) +
            "\r\n"
            "Content-type: image/jpeg\r\n"
            "\r\n" +
            body1 +
            "--boundaryLibAsyik00001\r\n"
            "x-mpart-id: 2\r\n"
            "Content-length: " +
            std::to_string(body2.length()) +
            "\r\n"
            "Content-type: image/jpeg\r\n"
            "\r\n" +
            body2 +
            "--boundaryLibAsyik00001\r\n"
            "x-mpart-id: 3\r\n"
            "Content-length: " +
            std::to_string(body3.length()) +
            "\r\n"
            "Content-type: image/jpeg\r\n"
            "\r\n" +
            body3 + "--boundaryLibAsyik00001--\r\n";

        asio::async_write(stream, asio::buffer(s.data(), s.length()),
                          use_fiber_future)
            .get();
      });
#endif

  as->execute([as]() {
    REQUIRE_THROWS_AS(
        http_easy_request(as, "GET", "http://127.0.0.1:4011/multipart"),
        asyik::unexpected_error);

    http_easy_request_multipart(
        as, "GET", "http://127.0.0.1:4011/multipart", [](http_request_ptr req) {
          REQUIRE_FALSE(
              req->response.headers["x-test-reply"].compare("amiiin"));
          std::string id = req->multipart_response.headers["x-mpart-id"];
          LOG(INFO) << "got multipart id=" << id << "\n";
          std::string body = std::to_string(std::stoi(id) * 100);

          REQUIRE_FALSE(req->multipart_response.body.compare(body));
        });

#ifdef LIBASYIK_ENABLE_SSL_SERVER
    // SSL Part
    REQUIRE_THROWS_AS(
        http_easy_request(as, "GET", "https://127.0.0.1:4012/multipart"),
        asyik::unexpected_error);

    http_easy_request_multipart(
        as, "GET", "https://127.0.0.1:4012/multipart",
        [](http_request_ptr req) {
          REQUIRE_FALSE(
              req->response.headers["x-test-reply"].compare("amiiin"));
          std::string id = req->multipart_response.headers["x-mpart-id"];
          LOG(INFO) << "got multipart id=" << id << "\n";
          std::string body = std::to_string(std::stoi(id) * 100);

          REQUIRE_FALSE(req->multipart_response.body.compare(body));
        });
#endif

    as->stop();
  }  // namespace asyik
  );

  as->run();
  LOG(INFO) << "testing multipart http client done\n";
  asyik::sleep_for(std::chrono::milliseconds(500));
}  // namespace asyik

TEST_CASE("Test websocket binary", "[http]")
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4006);
  std::vector<uint8_t> buff;
  for (int i = 0; i < 1024; i++) buff.push_back((uint8_t)rand() % 256);

  server->on_websocket("/binary_test", [&buff](websocket_ptr ws,
                                               const http_route_args& args) {
    try {
      auto s = ws->get_string();
      REQUIRE(!s.compare("this is text"));

      std::vector<uint8_t> b;
      b.resize(1024);
      ws->read_basic_buffer(b);
      REQUIRE(b == buff);

      auto s2 = ws->get_string();
      REQUIRE(!s2.compare("again, this is text"));

      try {
        auto s3 = ws->get_string();
        REQUIRE(false);
      } catch (asyik::unexpected_error& e) {
        LOG(INFO) << "catch exception because we got binary when we're "
                     "actually expect text(expected)\n";
      }

      auto s3 = ws->get_string();
      REQUIRE(!s3.compare("third, this is text"));

      std::vector<uint8_t> b2;
      b2.resize(2048);
      auto sz = ws->read_basic_buffer(b2);
      b2.resize(sz);  // change actual resulting output buffer size
      REQUIRE(b2 == buff);

      try {
        std::vector<uint8_t> b;
        b.resize(512);
        ws->read_basic_buffer(b);
        REQUIRE(false);
      } catch (boost::system::system_error& e) {
        LOG(INFO) << "got exception: " << e.what() << "(expected)\n";
      }
    } catch (std::exception& e) {
      LOG(INFO) << "got unexpected exception in handler binary_test, what()="
                << e.what() << "\n";
      REQUIRE(false);
    }
  });

  // check ws client timeout
  as->execute([as, &buff, server]() {
    asyik::sleep_for(std::chrono::milliseconds(50));

    asyik::websocket_ptr ws =
        asyik::make_websocket_connection(as, "ws://127.0.0.1:4006/binary_test");

    ws->send_string("this is text");
    ws->write_basic_buffer(buff);
    ws->send_string("again, this is text");
    ws->write_basic_buffer(buff);
    ws->send_string("third, this is text");
    ws->write_basic_buffer(buff);
    ws->write_basic_buffer(buff);

    asyik::sleep_for(std::chrono::milliseconds(500));
    // test what if, we dont close the server properly
    // server->close();
    as->stop();
    LOG(INFO) << "binary test completed\n";
  });

  as->run();
}

TEST_CASE("Test WS close handling", "[http]")
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4007);

#if __cplusplus >= 201402L
  server->on_websocket("/ws", [as](auto ws, auto args) {
#else
  server->on_websocket(
      "/ws", [as](websocket_ptr ws, const http_route_args& args) {
#endif
    try {
      REQUIRE(as == asyik::get_current_service());

      ws->set_idle_timeout(2);
      ws->set_keepalive_pings(true);

      while (1) {
        auto s = ws->get_string();
        ws->send_string(s);

        std::vector<uint8_t> b;
        b.resize(1 * 1024 * 1024);
        auto sz = ws->read_basic_buffer(b);
        b.resize(sz);
        ws->write_basic_buffer(b);
      }
    } catch (asyik::already_closed_error& e) {
    } catch (asyik::network_timeout_error& e) {
    } catch (boost::system::system_error& e) {
      // after socker closed, either the handle will no longer valid
      // or being used by other process
      REQUIRE(((e.code() == boost::asio::error::bad_descriptor) ||
               (e.code() == boost::asio::error::not_connected)));
    } catch (std::exception& e) {
      LOG(INFO) << "got ws exception, what: " << e.what() << "\n";
      REQUIRE(false);
    }
  });

#if __cplusplus >= 201402L
  server->on_websocket("/ws_close", [as](auto ws, auto args) {
#else
  server->on_websocket("/ws_close",
                       [as](websocket_ptr ws, const http_route_args& args) {
#endif
    ws->send_string("halo");
  });

  asyik::sleep_for(std::chrono::milliseconds(100));

  std::thread t([as_host = as]() {
    auto as = asyik::make_service();

    LOG(INFO) << "Testing ws close handling\n";
    int cnt = 32;
    for (int i = 0; i < 32; i++)
      as->execute([as, &cnt]() {
        LOG(INFO) << "Scenario 1: close without standby read\n";
        for (int n = 0; n < 50; n++) {
          try {
            asyik::websocket_ptr ws =
                asyik::make_websocket_connection(as, "ws://127.0.0.1:4007/ws");

            std::shared_ptr<std::vector<uint8_t>> binary =
                as->async([]() {
                    auto binary = std::make_shared<std::vector<uint8_t>>();
                    binary->resize(rand() % (128 * 1024) + 10240);
                    memset(&(*binary)[0], ' ', binary->size());

                    return binary;
                  }).get();

            for (int i = 0; i < 2; i++) {
              ws->send_string("halo");
              auto s = ws->get_string();
              REQUIRE(!s.compare("halo"));

              ws->write_basic_buffer(*binary);
              std::vector<uint8_t> b;
              b.resize(1 * 1024 * 1024);
              auto sz = ws->read_basic_buffer(b);
              b.resize(sz);
              REQUIRE(std::equal(binary->begin(), binary->end(), b.begin()));
              asyik::sleep_for(std::chrono::milliseconds(rand() % 5));
            }

            ws->close(websocket_close_code::normal, "closed normally");

            asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
          } catch (std::exception& e) {
            LOG(ERROR) << "websocket binary failed. What: " << e.what() << "\n";
            REQUIRE(false);
          }
          asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
        }

        LOG(INFO) << "Scenario 2: close with standby read(pre-close)\n";
        for (int n = 0; n < 50; n++) {
          try {
            asyik::websocket_ptr ws =
                asyik::make_websocket_connection(as, "ws://127.0.0.1:4007/ws");

            std::shared_ptr<std::vector<uint8_t>> binary =
                as->async([]() {
                    auto binary = std::make_shared<std::vector<uint8_t>>();
                    binary->resize(rand() % (128 * 1024) + 10240);
                    memset(&(*binary)[0], ' ', binary->size());

                    return binary;
                  }).get();

            for (int i = 0; i < 2; i++) {
              ws->send_string("halo");
              auto s = ws->get_string();
              REQUIRE(!s.compare("halo"));

              ws->write_basic_buffer(*binary);
              std::vector<uint8_t> b;
              b.resize(1 * 1024 * 1024);
              auto sz = ws->read_basic_buffer(b);
              b.resize(sz);
              REQUIRE(std::equal(binary->begin(), binary->end(), b.begin()));
              asyik::sleep_for(std::chrono::milliseconds(rand() % 5));
            }

            as->execute([ws]() {
              try {
                while (1) ws->get_string();
              } catch (const std::exception& e) {
              }
            });

            asyik::sleep_for(std::chrono::milliseconds((rand() % 5) + 0));
            ws->close(websocket_close_code::normal, "closed normally");
          } catch (std::exception& e) {
            LOG(ERROR) << "websocket binary failed. What: " << e.what() << "\n";
            REQUIRE(false);
          }
          asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
        }

        LOG(INFO) << "Scenario 3: close with standby read(after-close)\n";
        for (int n = 0; n < 50; n++) {
          try {
            asyik::websocket_ptr ws =
                asyik::make_websocket_connection(as, "ws://127.0.0.1:4007/ws");

            std::shared_ptr<std::vector<uint8_t>> binary =
                as->async([]() {
                    auto binary = std::make_shared<std::vector<uint8_t>>();
                    binary->resize(rand() % (128 * 1024) + 10240);
                    memset(&(*binary)[0], ' ', binary->size());

                    return binary;
                  }).get();

            for (int i = 0; i < 2; i++) {
              ws->send_string("halo");
              auto s = ws->get_string();
              REQUIRE(!s.compare("halo"));

              ws->write_basic_buffer(*binary);
              std::vector<uint8_t> b;
              b.resize(1 * 1024 * 1024);
              auto sz = ws->read_basic_buffer(b);
              b.resize(sz);
              REQUIRE(std::equal(binary->begin(), binary->end(), b.begin()));
              asyik::sleep_for(std::chrono::milliseconds(rand() % 5));
            }

            ws->close(websocket_close_code::normal, "closed normally");

            try {
              ws->get_string();
            } catch (...) {
            }

            asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
          } catch (std::exception& e) {
            LOG(ERROR) << "websocket binary failed. What: " << e.what() << "\n";
            REQUIRE(false);
          }
          asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
        }

        LOG(INFO) << "Scenario 4: No close\n";
        for (int n = 0; n < 50; n++) {
          try {
            asyik::websocket_ptr ws =
                asyik::make_websocket_connection(as, "ws://127.0.0.1:4007/ws");

            std::shared_ptr<std::vector<uint8_t>> binary =
                as->async([]() {
                    auto binary = std::make_shared<std::vector<uint8_t>>();
                    binary->resize(rand() % (128 * 1024) + 10240);
                    memset(&(*binary)[0], ' ', binary->size());

                    return binary;
                  }).get();

            for (int i = 0; i < 2; i++) {
              ws->send_string("halo");
              auto s = ws->get_string();
              REQUIRE(!s.compare("halo"));

              ws->write_basic_buffer(*binary);
              std::vector<uint8_t> b;
              b.resize(1 * 1024 * 1024);
              auto sz = ws->read_basic_buffer(b);
              b.resize(sz);
              REQUIRE(std::equal(binary->begin(), binary->end(), b.begin()));
              asyik::sleep_for(std::chrono::milliseconds(rand() % 5));
            }

            asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
          } catch (std::exception& e) {
            LOG(ERROR) << "websocket binary failed. What: " << e.what() << "\n";
            REQUIRE(false);
          }
          asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
        }

        LOG(INFO) << "Scenario 5: abrupt close by server\n";
        for (int n = 0; n < 25; n++) {
          asyik::websocket_ptr ws = asyik::make_websocket_connection(
              as, "ws://127.0.0.1:4007/ws_close");

          try {
            while (1) {
              auto s = ws->get_string();
              REQUIRE(!s.compare("halo"));
            }
          } catch (asyik::already_closed_error& e) {
          } catch (std::exception& e) {
            LOG(ERROR) << "websocket binary failed. What: " << e.what() << "\n";
            REQUIRE(false);
          }

          try {
            ws->close(websocket_close_code::normal, "closed normally");
          } catch (asyik::network_timeout_error& e) {
          } catch (std::exception& e) {
            LOG(ERROR) << "websocket close failed. What: " << e.what() << "\n";
            REQUIRE(false);
          }

          asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
        }

        if (!(--cnt)) as->stop();
      });

    as->run();
    as_host->stop();
  });

  t.detach();

  as->run();
}

// ---------------------------------------------------------------------------
// Raw-socket helpers used by the keep-alive tests.
// Because libasyik uses cooperative (fiber) scheduling on a single OS thread,
// we cannot do a blocking thread-join inside a fiber.  Instead we run the
// synchronous Boost.Beast client work in a background std::thread and spin
// the fiber with sleep_for(5ms) until the thread is done.  The yielding
// allows the libasyik server fibers to process the incoming requests.
// ---------------------------------------------------------------------------

namespace keepalive_helpers {

namespace net = boost::asio;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;

// Run `fn` in a background thread; from the *current fiber* poll with
// sleep_for until done, then join.  Returns any exception thrown by fn.
static std::exception_ptr run_bg(std::function<void()> fn)
{
  std::atomic<bool> done{false};
  std::exception_ptr ex;

  std::thread t([&] {
    try {
      fn();
    } catch (...) {
      ex = std::current_exception();
    }
    done.store(true, std::memory_order_release);
  });

  while (!done.load(std::memory_order_acquire))
    asyik::sleep_for(std::chrono::milliseconds(5));

  t.join();
  return ex;
}

// Open a sync TCP socket to addr:port (blocking, run inside background thread).
static tcp::socket connect_raw(net::io_context& ioc, const std::string& addr,
                               uint16_t port)
{
  tcp::socket sock(ioc);
  tcp::resolver resolver(ioc);
  auto results = resolver.resolve(addr, std::to_string(port));
  net::connect(sock, results);
  return sock;
}

// Send one HTTP request and return the response.  buf is reused across calls
// on the same keep-alive connection so framing state is preserved.
static http::response<http::string_body> one_request(
    tcp::socket& sock, boost::beast::flat_buffer& buf, http::verb verb,
    const std::string& target, int version /* 11=HTTP/1.1, 10=HTTP/1.0 */,
    bool explicit_close = false)
{
  http::request<http::string_body> req{verb, target, version};
  req.set(http::field::host, "127.0.0.1");
  if (explicit_close) req.set(http::field::connection, "close");
  req.prepare_payload();
  http::write(sock, req);

  http::response<http::string_body> res;
  http::read(sock, buf, res);
  return res;
}

}  // namespace keepalive_helpers

TEST_CASE("Test HTTP keep-alive and connection-close behavior",
          "[http][keepalive]")
{
  namespace http = boost::beast::http;
  namespace net = boost::asio;
  using tcp = net::ip::tcp;
  using namespace keepalive_helpers;

  auto as = asyik::make_service();
  // Port 4015 – not used by any other test case in this file.
  auto server = asyik::make_http_server(as, "127.0.0.1", 4015);

  std::atomic<int> n_handled{0};

  server->on_http_request(
      "/ping", "GET",
      [&n_handled](http_request_ptr req, const http_route_args&) {
        n_handled.fetch_add(1, std::memory_order_relaxed);
        req->response.body = "pong";
        req->response.result(200);
      });

  server->on_http_request("/echo", "POST",
                          [](http_request_ptr req, const http_route_args&) {
                            req->response.body = req->body;
                            req->response.result(200);
                          });

  // Route that returns the HTTP status code embedded in the URL so we can
  // drive 200 / 404 / 500 from the client side.
  server->on_http_request(
      "/status/<int>", "GET",
      [](http_request_ptr req, const http_route_args& args) {
        req->response.body = "status=" + std::string(args[1]);
        req->response.result(std::stoi(std::string(args[1])));
      });

  asyik::sleep_for(std::chrono::milliseconds(100));

  as->execute([&]() {
    // ------------------------------------------------------------------ //
    // 1.  HTTP/1.1 keep-alive: N requests on ONE TCP connection            //
    // ------------------------------------------------------------------ //
    {
      const int N = 8;
      int ok_count = 0;
      bool connection_reused = true;

      auto ex = run_bg([&] {
        net::io_context ioc;
        auto sock = connect_raw(ioc, "127.0.0.1", 4015);
        boost::beast::flat_buffer buf;

        for (int i = 0; i < N; i++) {
          auto res = one_request(sock, buf, http::verb::get, "/ping", 11);
          if (res.result() == http::status::ok) ok_count++;
          // Server must advertise keep-alive on HTTP/1.1.
          if (!res.keep_alive()) connection_reused = false;
        }

        // Graceful FIN
        boost::beast::error_code ec;
        sock.shutdown(tcp::socket::shutdown_both, ec);
      });

      if (ex) std::rethrow_exception(ex);
      REQUIRE(ok_count == N);
      REQUIRE(connection_reused);
      REQUIRE(n_handled.load() == N);
      INFO("keep-alive: " << N << " requests served on one connection");
    }

    // ------------------------------------------------------------------ //
    // 2.  Response state must NOT bleed between keep-alive requests.       //
    //     Sequence: 200 → 404 → 200 → 500 → 200 on one socket.           //
    // ------------------------------------------------------------------ //
    {
      n_handled.store(0);
      struct Expect {
        std::string route;
        int code;
      };
      std::vector<Expect> seq = {
          {"/ping", 200},       {"/status/404", 404}, {"/ping", 200},
          {"/status/500", 500}, {"/ping", 200},
      };

      std::vector<int> got_codes;
      auto ex = run_bg([&] {
        net::io_context ioc;
        auto sock = connect_raw(ioc, "127.0.0.1", 4015);
        boost::beast::flat_buffer buf;

        for (auto& e : seq) {
          auto res = one_request(sock, buf, http::verb::get, e.route, 11);
          got_codes.push_back((int)res.result_int());
        }

        boost::beast::error_code ec;
        sock.shutdown(tcp::socket::shutdown_both, ec);
      });

      if (ex) std::rethrow_exception(ex);
      REQUIRE(got_codes.size() == seq.size());
      for (size_t i = 0; i < seq.size(); i++) {
        INFO("request " << i << " expected " << seq[i].code << " got "
                        << got_codes[i]);
        REQUIRE(got_codes[i] == seq[i].code);
      }
    }

    // ------------------------------------------------------------------ //
    // 3.  Connection: close — server must close after one response.        //
    // ------------------------------------------------------------------ //
    {
      bool server_closed = false;
      auto ex = run_bg([&] {
        net::io_context ioc;
        auto sock = connect_raw(ioc, "127.0.0.1", 4015);
        boost::beast::flat_buffer buf;

        // First request with explicit Connection: close
        auto res = one_request(sock, buf, http::verb::get, "/ping", 11,
                               /*explicit_close=*/true);
        REQUIRE(res.result() == http::status::ok);
        // Server side: need_eof() must be true → server already sent FIN
        REQUIRE(!res.keep_alive());

        // A second read must hit EOF immediately.
        boost::beast::error_code ec;
        http::response<http::string_body> res2;
        http::read(sock, buf, res2, ec);
        server_closed = (ec == http::error::end_of_stream ||
                         ec == boost::asio::error::eof ||
                         ec == boost::asio::error::connection_reset);
      });

      if (ex) std::rethrow_exception(ex);
      REQUIRE(server_closed);
      INFO("Connection: close – server closed TCP after one response");
    }

    // ------------------------------------------------------------------ //
    // 4.  HTTP/1.0 — keep-alive is off by default; server closes after    //
    //     each response.                                                   //
    // ------------------------------------------------------------------ //
    {
      bool server_closed = false;
      auto ex = run_bg([&] {
        net::io_context ioc;
        auto sock = connect_raw(ioc, "127.0.0.1", 4015);
        boost::beast::flat_buffer buf;

        // HTTP/1.0 request – no keep-alive negotiated
        auto res = one_request(sock, buf, http::verb::get, "/ping", 10);
        REQUIRE(res.result() == http::status::ok);
        // Server must not advertise keep-alive for HTTP/1.0
        REQUIRE(!res.keep_alive());

        boost::beast::error_code ec;
        http::response<http::string_body> res2;
        http::read(sock, buf, res2, ec);
        server_closed = (ec == http::error::end_of_stream ||
                         ec == boost::asio::error::eof ||
                         ec == boost::asio::error::connection_reset);
      });

      if (ex) std::rethrow_exception(ex);
      REQUIRE(server_closed);
      INFO("HTTP/1.0 – server closed TCP after one response");
    }

    // ------------------------------------------------------------------ //
    // 5.  POST body is not corrupted across keep-alive requests.           //
    // ------------------------------------------------------------------ //
    {
      const int N = 6;
      int ok_count = 0;
      auto ex = run_bg([&] {
        net::io_context ioc;
        auto sock = connect_raw(ioc, "127.0.0.1", 4015);
        boost::beast::flat_buffer buf;

        for (int i = 0; i < N; i++) {
          std::string payload = "body-payload-" + std::to_string(i);
          http::request<http::string_body> req{http::verb::post, "/echo", 11};
          req.set(http::field::host, "127.0.0.1");
          req.body() = payload;
          req.prepare_payload();
          http::write(sock, req);

          http::response<http::string_body> res;
          http::read(sock, buf, res);
          if (res.result() == http::status::ok && res.body() == payload)
            ok_count++;
        }

        boost::beast::error_code ec;
        sock.shutdown(tcp::socket::shutdown_both, ec);
      });

      if (ex) std::rethrow_exception(ex);
      REQUIRE(ok_count == N);
      INFO("POST echo: body intact across " << N << " keep-alive requests");
    }

    // ------------------------------------------------------------------ //
    // 6.  Stress: 50 requests on one connection.                          //
    // ------------------------------------------------------------------ //
    {
      const int N = 50;
      int ok_count = 0;
      n_handled.store(0);
      auto ex = run_bg([&] {
        net::io_context ioc;
        auto sock = connect_raw(ioc, "127.0.0.1", 4015);
        boost::beast::flat_buffer buf;

        for (int i = 0; i < N; i++) {
          auto res = one_request(sock, buf, http::verb::get, "/ping", 11);
          if (res.result() == http::status::ok) ok_count++;
        }

        boost::beast::error_code ec;
        sock.shutdown(tcp::socket::shutdown_both, ec);
      });

      if (ex) std::rethrow_exception(ex);
      REQUIRE(ok_count == N);
      REQUIRE(n_handled.load() == N);
      INFO("stress: " << N << " requests on one connection, all OK");
    }

    server->close();
    as->stop();
  });

  as->run();
}

TEST_CASE("Test http url view", "[http_url_view]")
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4006);

  server->on_http_request(
      "/name/<string>", "GET",
      [](http_request_ptr req, const http_route_args& args) {
        req->response.headers.set("x-test-reply", "amiiin");
        req->response.headers.set("content-type", "text/json");

        auto uv = req->get_url_view();
        std::string params = "-";
        for (auto x : uv.params()) {
          params += x.key + "=" + x.value + ",";
        }
        req->response.body = "GET-" + args[1] + params;
        req->response.result(200);
      });

  as->execute([as, server]() {
    auto req =
        asyik::http_easy_request(as, "GET", "http://127.0.0.1:4006/name/999/");

    REQUIRE(req->response.result() == 200);
    REQUIRE(!req->response.body.compare("GET-999-"));
    REQUIRE(!req->response.headers["x-test-reply"].compare("amiiin"));

    req = asyik::http_easy_request(
        as, "GET", "http://127.0.0.1:4006/name/999/?dummy1=2&dummy2=haha");

    REQUIRE(req->response.result() == 200);
    REQUIRE(!req->response.body.compare("GET-999-dummy1=2,dummy2=haha,"));
    REQUIRE(!req->response.headers["x-test-reply"].compare("amiiin"));

    req = asyik::http_easy_request(
        as, "GET", "http://127.0.0.1:4006/name/999?dummy1=2&dummy2=haha");

    REQUIRE(req->response.result() == 200);
    REQUIRE(!req->response.body.compare("GET-999-dummy1=2,dummy2=haha,"));
    REQUIRE(!req->response.headers["x-test-reply"].compare("amiiin"));

    req = asyik::http_easy_request(
        as, "GET",
        "http://127.0.0.1:4006/name/999?dummy1=2&dummy2=haha#testfragment");

    REQUIRE(req->response.result() == 200);
    REQUIRE(!req->response.body.compare("GET-999-dummy1=2,dummy2=haha,"));
    REQUIRE(!req->response.headers["x-test-reply"].compare("amiiin"));

    req = asyik::http_easy_request(
        as, "GET", "http://127.0.0.1:4006/name/999/??dummy1=2&dummy2=haha");
    REQUIRE(req->response.result() == 404);

    req = asyik::http_easy_request(
        as, "GET", "http://127.0.0.1:4006/name/999/?dummy1=?&dummy2=?");
    REQUIRE(req->response.result() == 404);

    req = asyik::http_easy_request(as, "GET",
                                   "http://127.0.0.1:4006/name/99?9??9");
    REQUIRE(req->response.result() == 404);

    server->close();
    // as->stop();
  });

  as->run(true);
}

TEST_CASE("check route insert_front ordering")
{
  auto as = asyik::make_service();
  auto server = asyik::make_http_server(as, "127.0.0.1", 4006);

  // Register a catch-all route first
  server->on_http_request_regex(std::regex("^/.*$"), "",
                                [](auto req, auto args) {
                                  req->response.body = "catch-all";
                                  req->response.result(200);
                                });

  // Register a specific route with insert_front=true so it takes priority
  server->on_http_request(
      "/api/<string>", "GET",
      [](auto req, auto args) {
        req->response.body = "api:" + args[1];
        req->response.result(200);
      },
      /*insert_front=*/true);

  as->execute([as]() {
    auto req =
        asyik::http_easy_request(as, "GET", "http://127.0.0.1:4006/api/hello");
    REQUIRE(req->response.result() == 200);
    REQUIRE(req->response.body == "api:hello");

    // non-api path still hits the catch-all
    auto req2 =
        asyik::http_easy_request(as, "GET", "http://127.0.0.1:4006/other");
    REQUIRE(req2->response.result() == 200);
    REQUIRE(req2->response.body == "catch-all");

    as->stop();
  });

  as->run();
}

// ───────────────────────────────────────────────────────────────────
// route_table unit tests
// ───────────────────────────────────────────────────────────────────

// Lightweight request stub for testing route_table::find() without a full
// Beast request.
struct stub_request {
  std::string target_;
  std::string method_;
  std::string target() const { return target_; }
  std::string method_string() const { return method_; }
};

// Helper: build an http_route_tuple from a route_spec string and optional
// method.  The callback is a no-op; we only care about matching.
static http_route_tuple make_route(string_view route_spec,
                                   string_view method = "")
{
  std::regex re(internal::route_spec_to_regex(route_spec));
  return http_route_tuple{std::string{method.data(), method.size()},
                          std::move(re), http_route_callback{}};
}

// ── Helper function tests ──

TEST_CASE("is_static_route identifies static vs parameterized routes",
          "[route_table]")
{
  REQUIRE(is_static_route("/api/v1/users") == true);
  REQUIRE(is_static_route("/") == true);
  REQUIRE(is_static_route("/health") == true);
  REQUIRE(is_static_route("/api/v1/users/<int>") == false);
  REQUIRE(is_static_route("/<string>/items") == false);
  REQUIRE(is_static_route("/files/<path>") == false);
}

TEST_CASE("extract_static_prefix returns correct prefixes", "[route_table]")
{
  // Fully static — returns normalised (no trailing slash)
  REQUIRE(extract_static_prefix("/api/v1/users") == "/api/v1/users");
  REQUIRE(extract_static_prefix("/api/v1/users/") == "/api/v1/users");
  REQUIRE(extract_static_prefix("/") == "");

  // Parameterized — returns prefix up to first '<'
  REQUIRE(extract_static_prefix("/api/v1/users/<int>") == "/api/v1/users/");
  REQUIRE(extract_static_prefix("/api/<string>/items") == "/api/");
  REQUIRE(extract_static_prefix("/<int>") == "/");
}

TEST_CASE("normalise_path strips query string and trailing slash",
          "[route_table]")
{
  REQUIRE(normalise_path("/api/v1/users") == "/api/v1/users");
  REQUIRE(normalise_path("/api/v1/users/") == "/api/v1/users");
  REQUIRE(normalise_path("/api?foo=bar") == "/api");
  REQUIRE(normalise_path("/api/v1/?x=1&y=2") == "/api/v1");
  // Root stays as "/"
  REQUIRE(normalise_path("/") == "/");
  // No query, no trailing slash
  REQUIRE(normalise_path("/health") == "/health");
}

// ── Tier 1: exact match ──

TEST_CASE("route_table tier 1 exact match for static routes", "[route_table]")
{
  route_table<http_route_tuple> table;
  table.add_route("/api/v1/users", make_route("/api/v1/users", "GET"));
  table.add_route("/health", make_route("/health"));

  http_route_args args;

  // Exact match
  stub_request req{"/api/v1/users", "GET"};
  const auto& found = table.find(req, args);
  REQUIRE(std::get<0>(found) == "GET");
  REQUIRE(args.size() == 1);
  REQUIRE(args[0] == "/api/v1/users");

  // Trailing slash normalisation
  stub_request req2{"/api/v1/users/", "GET"};
  const auto& found2 = table.find(req2, args);
  REQUIRE(std::get<0>(found2) == "GET");

  // Query string stripping
  stub_request req3{"/api/v1/users?page=1", "GET"};
  const auto& found3 = table.find(req3, args);
  REQUIRE(std::get<0>(found3) == "GET");

  // Method-agnostic route (empty method matches any)
  stub_request req4{"/health", "POST"};
  const auto& found4 = table.find(req4, args);
  REQUIRE(std::get<0>(found4).empty());
}

TEST_CASE("route_table tier 1 method filtering", "[route_table]")
{
  route_table<http_route_tuple> table;
  table.add_route("/api/data", make_route("/api/data", "GET"));
  table.add_route("/api/data", make_route("/api/data", "POST"));

  http_route_args args;

  stub_request get_req{"/api/data", "GET"};
  const auto& get_found = table.find(get_req, args);
  REQUIRE(std::get<0>(get_found) == "GET");

  stub_request post_req{"/api/data", "POST"};
  const auto& post_found = table.find(post_req, args);
  REQUIRE(std::get<0>(post_found) == "POST");

  // DELETE not registered — should throw
  stub_request del_req{"/api/data", "DELETE"};
  REQUIRE_THROWS_AS(table.find(del_req, args), not_found_error);
}

// ── Tier 2: prefix + regex ──

TEST_CASE("route_table tier 2 parameterized route matching", "[route_table]")
{
  route_table<http_route_tuple> table;
  table.add_route("/api/v1/users/<int>",
                  make_route("/api/v1/users/<int>", "GET"));
  table.add_route("/api/v1/<string>/items",
                  make_route("/api/v1/<string>/items", "GET"));

  http_route_args args;

  // <int> capture
  stub_request req1{"/api/v1/users/42", "GET"};
  const auto& found1 = table.find(req1, args);
  REQUIRE(std::get<0>(found1) == "GET");
  REQUIRE(args.size() >= 2);
  REQUIRE(args[1] == "42");

  // <string> capture
  stub_request req2{"/api/v1/widgets/items", "GET"};
  const auto& found2 = table.find(req2, args);
  REQUIRE(args[1] == "widgets");

  // Non-matching prefix — should not match
  stub_request req3{"/other/v1/users/42", "GET"};
  REQUIRE_THROWS_AS(table.find(req3, args), not_found_error);
}

TEST_CASE("route_table tier 2 prefix check skips non-matching entries",
          "[route_table]")
{
  route_table<http_route_tuple> table;
  table.add_route("/alpha/<int>", make_route("/alpha/<int>", "GET"));
  table.add_route("/beta/<string>", make_route("/beta/<string>", "GET"));

  http_route_args args;

  // Should match /beta/, not /alpha/
  stub_request req{"/beta/hello", "GET"};
  const auto& found = table.find(req, args);
  REQUIRE(args[1] == "hello");

  // /alpha/ with non-numeric should NOT match <int>
  stub_request req2{"/alpha/abc", "GET"};
  REQUIRE_THROWS_AS(table.find(req2, args), not_found_error);
}

// ── Tier 3: fallback regex ──

TEST_CASE("route_table tier 3 fallback regex routes", "[route_table]")
{
  route_table<http_route_tuple> table;
  // Raw regex — no route_spec structure
  auto route = http_route_tuple{"GET", std::regex(R"(^/files/(.+)$)"),
                                http_route_callback{}};
  table.add_regex_route(std::move(route));

  http_route_args args;

  stub_request req{"/files/docs/readme.txt", "GET"};
  const auto& found = table.find(req, args);
  REQUIRE(std::get<0>(found) == "GET");
  REQUIRE(args.size() >= 2);
  REQUIRE(args[1] == "docs/readme.txt");

  stub_request req2{"/other/path", "GET"};
  REQUIRE_THROWS_AS(table.find(req2, args), not_found_error);
}

// ── Cross-tier priority ──

TEST_CASE("route_table tier priority: exact > prefix > fallback",
          "[route_table]")
{
  route_table<http_route_tuple> table;

  // Tier 3 fallback: catch-all
  auto fallback =
      http_route_tuple{"", std::regex(R"(^/api.*$)"), http_route_callback{}};
  table.add_regex_route(std::move(fallback));

  // Tier 2 parameterized
  table.add_route("/api/users/<int>", make_route("/api/users/<int>", "GET"));

  // Tier 1 exact
  table.add_route("/api/users/42", make_route("/api/users/42", "GET"));

  http_route_args args;

  // Should match exact (Tier 1), not parameterized or fallback
  stub_request req1{"/api/users/42", "GET"};
  const auto& found1 = table.find(req1, args);
  // Tier 1 returns path as args[0], no capture groups
  REQUIRE(args.size() == 1);
  REQUIRE(args[0] == "/api/users/42");

  // Should match Tier 2 (parameterized), not fallback
  stub_request req2{"/api/users/99", "GET"};
  const auto& found2 = table.find(req2, args);
  REQUIRE(args.size() >= 2);
  REQUIRE(args[1] == "99");

  // Should fall through to Tier 3 (fallback)
  stub_request req3{"/api/other", "GET"};
  const auto& found3 = table.find(req3, args);
  // Fallback match — came from regex_route
  REQUIRE(args[0].find("/api") != std::string::npos);
}

// ── insert_front ordering ──

TEST_CASE("route_table insert_front within same tier", "[route_table]")
{
  // Tier 2: two parameterized routes with overlapping prefix
  route_table<http_route_tuple> table;
  table.add_route("/api/<string>", make_route("/api/<string>", "GET"));
  // This one is inserted at front, should match first
  table.add_route("/api/<int>", make_route("/api/<int>", "GET"),
                  /*insert_front=*/true);

  http_route_args args;

  // "123" matches both <int> and <string>; <int> inserted first, wins
  stub_request req{"/api/123", "GET"};
  const auto& found = table.find(req, args);
  REQUIRE(args[1] == "123");

  // Tier 1: exact routes with same path, different methods
  route_table<http_route_tuple> table2;
  table2.add_route("/data", make_route("/data", "GET"));
  table2.add_route("/data", make_route("/data", "POST"),
                   /*insert_front=*/true);

  // POST inserted at front — for a POST request it should match first
  stub_request post_req{"/data", "POST"};
  const auto& found2 = table2.find(post_req, args);
  REQUIRE(std::get<0>(found2) == "POST");

  // Tier 3: fallback insert_front
  route_table<http_route_tuple> table3;
  auto r1 = http_route_tuple{"GET", std::regex(R"(^/x/(.*)$)"),
                             http_route_callback{}};
  table3.add_regex_route(std::move(r1));
  auto r2 = http_route_tuple{"GET", std::regex(R"(^/x/special$)"),
                             http_route_callback{}};
  table3.add_regex_route(std::move(r2), /*insert_front=*/true);

  stub_request xreq{"/x/special", "GET"};
  const auto& found3 = table3.find(xreq, args);
  // The front-inserted route matches "special" exactly
  REQUIRE(args[0] == "/x/special");
}

// ── empty table / not found ──

TEST_CASE("route_table empty table throws not_found_error", "[route_table]")
{
  route_table<http_route_tuple> table;
  REQUIRE(table.empty());

  http_route_args args;
  stub_request req{"/anything", "GET"};
  REQUIRE_THROWS_AS(table.find(req, args), not_found_error);
}

TEST_CASE("route_table empty() reflects state correctly", "[route_table]")
{
  route_table<http_route_tuple> table;
  REQUIRE(table.empty());

  table.add_route("/x", make_route("/x"));
  REQUIRE_FALSE(table.empty());
}

// ── Method case-insensitivity ──

TEST_CASE("route_table method matching is case-insensitive", "[route_table]")
{
  route_table<http_route_tuple> table;
  table.add_route("/api", make_route("/api", "GET"));

  http_route_args args;

  stub_request req{"/api", "get"};
  REQUIRE_NOTHROW(table.find(req, args));

  stub_request req2{"/api", "Get"};
  REQUIRE_NOTHROW(table.find(req2, args));
}

// ── <path> tag matching ──

TEST_CASE("route_table path tag captures slashes", "[route_table]")
{
  route_table<http_route_tuple> table;
  table.add_route("/static/<path>", make_route("/static/<path>", "GET"));

  http_route_args args;

  stub_request req{"/static/css/style.css", "GET"};
  const auto& found = table.find(req, args);
  REQUIRE(args.size() >= 2);
  REQUIRE(args[1] == "css/style.css");
}

// ── Integration: use with real http_beast_request ──

TEST_CASE("route_table works with real http_beast_request", "[route_table]")
{
  route_table<http_route_tuple> table;
  table.add_route("/api/v1/health", make_route("/api/v1/health", "GET"));
  table.add_route("/api/v1/users/<int>",
                  make_route("/api/v1/users/<int>", "GET"));

  http_route_args args;

  // Use a real Beast request object
  http_beast_request beast_req;
  beast_req.method(boost::beast::http::verb::get);
  beast_req.target("/api/v1/health");

  const auto& found = table.find(beast_req, args);
  REQUIRE(std::get<0>(found) == "GET");
  REQUIRE(args[0] == "/api/v1/health");

  // Parameterized with Beast request
  http_beast_request beast_req2;
  beast_req2.method(boost::beast::http::verb::get);
  beast_req2.target("/api/v1/users/7");

  const auto& found2 = table.find(beast_req2, args);
  REQUIRE(args[1] == "7");
}

// ───────────────────────────────────────────────────────────────────
// Regression test: service must terminate even when a keep-alive client
// holds the connection open after server->close() / service::stop().
// ───────────────────────────────────────────────────────────────────

TEST_CASE("service terminates with stuck keep-alive client",
          "[http][termination]")
{
  namespace asio = boost::asio;
  namespace beast = boost::beast;
  namespace bhttp = boost::beast::http;
  using tcp = boost::asio::ip::tcp;

  const uint16_t port = 4321;
  auto service = asyik::make_service();
  auto server = asyik::make_http_server(service, "127.0.0.1", port);

  server->on_http_request(
      "/", "GET",
      [](asyik::http_request_ptr req, const asyik::http_route_args&) {
        req->response.body = "ok";
        req->response.headers.set("Content-Type", "text/plain");
        req->response.result(200);
      });

  std::atomic<bool> client_received{false};
  std::atomic<bool> client_should_exit{false};

  std::thread client([&]() {
    try {
      asio::io_context io;
      tcp::resolver resolver(io);
      beast::tcp_stream stream(io);
      auto endpoints = resolver.resolve("127.0.0.1", std::to_string(port));
      stream.connect(endpoints);

      bhttp::request<bhttp::empty_body> req{bhttp::verb::get, "/", 11};
      req.set(bhttp::field::host, "127.0.0.1");
      req.set(bhttp::field::connection, "keep-alive");
      req.keep_alive(true);
      bhttp::write(stream, req);

      beast::flat_buffer buffer;
      bhttp::response<bhttp::string_body> res;
      bhttp::read(stream, buffer, res);

      client_received.store(true, std::memory_order_release);

      // Hold the keep-alive connection open until the test signals exit.
      while (!client_should_exit.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    } catch (...) {
      // ignore
    }
  });

  asio::steady_timer stop_timer(service->get_io_service());
  std::function<void()> arm_stop_timer;
  arm_stop_timer = [&]() {
    stop_timer.expires_after(std::chrono::milliseconds(50));
    stop_timer.async_wait([&](const boost::system::error_code& ec) {
      if (ec) return;
      if (!client_received.load(std::memory_order_acquire)) {
        arm_stop_timer();
        return;
      }
      server->close();
      service->stop();
    });
  };
  arm_stop_timer();

  auto t0 = std::chrono::steady_clock::now();
  service->run();
  auto t1 = std::chrono::steady_clock::now();
  auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

  // The service must terminate within a bounded time despite the keep-alive
  // client still holding the connection open.
  INFO("service->run() returned after " << ms << "ms");
  REQUIRE(ms < 5000);

  client_should_exit.store(true, std::memory_order_release);
  if (client.joinable()) client.join();
}

}  // namespace asyik