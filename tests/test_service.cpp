
#include "catch2/catch.hpp"
#include "libasyik/service.hpp"

namespace asyik{
  void _TEST_invoke_service(){};

  
  TEST_CASE("very basic fiber execution", "[service]")
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

  TEST_CASE("Check spawn() and execute()", "[service]")
  {
    auto as = asyik::make_service();
    std::string sequence;
    as->execute([&] {
      sequence += "A";
      asyik::sleep_for(std::chrono::milliseconds(10));
      sequence += "A";
      asyik::sleep_for(std::chrono::milliseconds(20));
      sequence += "A";
    });
    auto fb = as->spawn([&] {
      asyik::sleep_for(std::chrono::milliseconds(20));
      sequence += "B";
      asyik::sleep_for(std::chrono::milliseconds(20));
      sequence += "B";
    });
    fb.join();
    REQUIRE(!sequence.compare("AABAB"));
  }

  TEST_CASE("execute async", "[service]")
  {
    auto as = asyik::make_service();
    std::atomic<int> i(0);

    as->async([&i](int inp) {
        usleep(100);
        i = inp;
      },
              10)
        .get();
    as->async([](auto &i) -> void {
      usleep(0);
      i++;
    },
              i);
    as->async([&i](string_view s, const std::string s2) -> void {
        usleep(130);
        int k = i + s.length() + s2.length();
        i = k;
      },
              "lol", "ok")
        .get();

    REQUIRE(i == 16);

    as->stop();
    as->run();
  }

  TEST_CASE("execute async with return value", "[service]")
  {
    auto as = asyik::make_service();

    std::string s =
        as->async([](int inp) {
            usleep(100);
            return std::to_string(inp);
          },
                  10)
            .get();

    REQUIRE(!s.compare("10"));

    as->stop();
    as->run();
  }
}