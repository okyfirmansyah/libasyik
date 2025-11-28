#include "catch2/catch.hpp"
#include "libasyik/http.hpp"
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

TEST_CASE("Stress Test HTTP", "[http]")
{
  auto as = asyik::make_service();

  auto server = asyik::make_http_server(as, "127.0.0.1", 4004);

#if __cplusplus >= 201402L
  server->on_websocket("/<int>/name/<string>", [as](auto ws, auto args) {
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
    } catch (std::exception& e) {
      LOG(INFO) << "got ws exception, what: " << e.what() << "\n";
    }
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

        as->async([req]() { req->response.body = req->body; }).get();
        req->response.result(200);
      });

  server->set_request_body_limit(20 * 1024 * 1024);
  server->set_request_header_limit(1024);

  asyik::sleep_for(std::chrono::milliseconds(100));

  std::thread t([]() {
    auto as = asyik::make_service();
    as->execute([as]() {
      while (1) {
        auto req = asyik::http_easy_request(
            as, "POST",
            "http://127.0.0.1:4004/post-only/30/name/listed?dummy=hihi", "hehe",
            {{"x-test", "ok"}});
        REQUIRE(req->response.result() == 200);
        std::string x_test_reply{req->response.headers["x-test-reply"]};
        std::string body = req->response.body;
        REQUIRE(!body.compare("hehe-30-listed-ok"));
        REQUIRE(!x_test_reply.compare("amiiin"));

        asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
      }
    });

    as->execute([as]() {
      while (1) {
        as->execute([as]() {
          auto req =
              asyik::http_easy_request(as, "GET",
                                       "http://nvm_user:nvm_pwd@127.0.0.1:4004/"
                                       "any/999/?dummy1&dummy2=ha+ha");

          REQUIRE(req->response.result() == 200);
          REQUIRE(!req->response.body.compare("-GET-999-"));
          REQUIRE(!req->response.headers["x-test-reply"].compare("amiin"));
        });
        as->execute([as]() {
          auto req =
              asyik::http_easy_request(as, "GET",
                                       "http://nvm_user:nvm_pwd@127.0.0.1:4004/"
                                       "any/999/?dummy1&dummy2=ha+ha");

          REQUIRE(req->response.result() == 200);
          REQUIRE(!req->response.body.compare("-GET-999-"));
          REQUIRE(!req->response.headers["x-test-reply"].compare("amiin"));
        });
        asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
      }
    });
    as->execute([as]() {
      while (1) {
        auto req = asyik::http_easy_request(
            as, "POST",
            "http://127.0.0.1:4004/post-only/30/name/listed?dummy=hihi", "hehe",
            {{"x-test", "ok"}});
        REQUIRE(req->response.result() == 200);
        std::string x_test_reply{req->response.headers["x-test-reply"]};
        std::string body = req->response.body;
        REQUIRE(!body.compare("hehe-30-listed-ok"));
        REQUIRE(!x_test_reply.compare("amiiin"));

        asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
      }
    });

    for (int i = 0; i < 64; i++)
      as->execute([as]() {
        while (1) {
          std::shared_ptr<std::string> binary =
              as->async([]() {
                  auto binary = std::make_shared<std::string>();
                  binary->resize(rand() % (128 * 1024) + 10240);
                  memset(&(*binary)[0], ' ', binary->size());

                  return binary;
                }).get();
          try {
            auto req = asyik::http_easy_request(
                as, "GET", "http://127.0.0.1:4004/big_size", *binary, {});
            REQUIRE(req->response.result() == 200);
            REQUIRE(req->response.body.length() == req->body.length());
          } catch (std::exception& e) {
            LOG(ERROR) << "test big size payload failed. What: " << e.what()
                       << "\n";
            REQUIRE(false);
          }
          asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
        }
      });

    for (int i = 0; i < 64; i++)
      as->execute([as]() {
        while (1) {
          try {
            asyik::websocket_ptr ws =
                asyik::make_websocket_connection(as, "ws://127.0.0.1:4004/ws");

            std::shared_ptr<std::vector<uint8_t>> binary =
                as->async([]() {
                    auto binary = std::make_shared<std::vector<uint8_t>>();
                    binary->resize(rand() % (128 * 1024) + 10240);
                    memset(&(*binary)[0], ' ', binary->size());

                    return binary;
                  }).get();

            for (int i = 0; i < 8; i++) {
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

            asyik::sleep_for(std::chrono::milliseconds(rand() % 5));
            ws->close(websocket_close_code::normal, "closed normally");

            // try {
            //   ws->get_string();
            // } catch (...) {
            // }

            LOG(INFO) << "success\n";
          } catch (std::exception& e) {
            LOG(ERROR) << "websocket binary failed. What: " << e.what() << "\n";
            REQUIRE(false);
          }
          asyik::sleep_for(std::chrono::milliseconds(rand() % 10));
        }
      });

    as->run();
  });

  t.detach();

  as->run();
}  // namespace asyik

}  // namespace asyik