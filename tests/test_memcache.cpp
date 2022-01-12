#include "libasyik/memcache.hpp"
#include "libasyik/error.hpp"
#include "libasyik/service.hpp"
#include "catch2/catch.hpp"

namespace asyik 
{

void _TEST_invoke_memcache(){};

TEST_CASE("Testing basic of memcache") 
{
  auto as = asyik::make_service();
  auto cache = asyik::make_memcache<std::string, int, 5>(as);

  // basic put-get
  cache->put("1", 1);
  cache->put("2", 2);
  cache->put("3", 3);

  REQUIRE(cache->at("1")==1);
  REQUIRE(cache->at("2")==2);
  REQUIRE(cache->at("3")==3);

  REQUIRE(cache->get("1")==1);
  REQUIRE(cache->get("2")==2);
  REQUIRE(cache->get("3")==3);

  // try out of range
  try
  {
    auto i = cache->at("4");

    REQUIRE(false);
  }catch(std::out_of_range &e)
  {
    REQUIRE(true);
  }

  try
  {
    auto i = cache->get("4");

    REQUIRE(false);
  }catch(std::out_of_range &e)
  {
    REQUIRE(true);
  }

  cache->put("3", 5);
  REQUIRE(cache->get("3")==5);
  REQUIRE(cache->at("3")==5);

  // try erasing an item
  cache->erase("1");
  try
  {
    auto i = cache->at("1");

    REQUIRE(false);
  }catch(std::out_of_range &e)
  {
    REQUIRE(true);
  }

  // try clearing all entries
  cache->clear();
  try
  {
    auto i = cache->at("2");

    REQUIRE(false);
  }catch(std::out_of_range &e)
  {
    REQUIRE(true);
  }

  // try creating memcache for movable only items
  auto cache2 = asyik::make_memcache<std::string, std::unique_ptr<std::string>, 1>(as);
  cache2->put("1", std::move(std::make_unique<std::string>("test")));
  LOG(INFO)<<"test priontout cache2:"<<*(cache2->at("1"))<<"\n";
  REQUIRE(!strcmp("test", cache2->at("1")->c_str()));
  asyik::sleep_for(std::chrono::milliseconds(500));
  cache2->put("1", std::move(std::make_unique<std::string>("test2")));
  LOG(INFO)<<"test priontout cache2:"<<*(cache2->at("1"))<<"\n";
  REQUIRE(!strcmp("test2", cache2->get("1")->c_str()));
  LOG(INFO)<<"test priontout cache2:"<<*(cache2->at("1"))<<"\n";
  REQUIRE(!strcmp("test2", cache2->at("1")->c_str()));
  asyik::sleep_for(std::chrono::milliseconds(1100));
  try //passive pruning
  {
    auto i = cache->at("1");

    REQUIRE(false);
  }catch(std::out_of_range &e)
  {
    REQUIRE(true);
  }

  // cache->get("1"); //=> throw error
  // cache->at("1"); //=> throw error
  // cache->clear();
  // cache->size();
  // cache->erase();

  // cache->count("1"); //=> doesnt update

  as->stop();
  as->run();
}


TEST_CASE("Testing active pruning mechanism") 
{
  auto as = asyik::make_service();
  auto cache = asyik::make_memcache<std::string, int, 1, 50>(as);

  as->execute([cache, as]()
  {
    // test basic timeouts
    cache->put("1", 11);
    cache->put("2", 22);
    cache->put("3", 3);
    asyik::sleep_for(std::chrono::milliseconds(960));

    REQUIRE(cache->get("1")==11);
    cache->put("3", 33); // update and relocate "3" to more recent partition
    asyik::sleep_for(std::chrono::milliseconds(960));

    REQUIRE(cache->get("1")==11);
    REQUIRE(cache->get("3")==33);
    // 2 should be out of range
    try
    {
      auto i = cache->at("2");

      REQUIRE(false);
    }catch(std::out_of_range &e)
    {
      REQUIRE(true);
    }
    
    asyik::sleep_for(std::chrono::milliseconds(1040));

    // now "1" should be out of range too
    try
    {
      auto i = cache->get("1");

      REQUIRE(false);
    }catch(std::out_of_range &e)
    {
      REQUIRE(true);
    }

    as->stop();
  });

  as->run();
}

TEST_CASE("Testing multithreading") 
{
  auto as = asyik::make_service();
  auto cache = asyik::make_memcache_mt<int, int, 1, 50>(as);

  std::atomic<int> num_done{0};
  for(int i=0;i<64;i++)
    as->async([cache, as, i, &num_done]()
    {
      // test basic timeouts
      cache->put(i*10 + 1, 11);
      cache->put(i*10 + 2, 22);
      cache->put(i*10 + 3, 3);
      asyik::sleep_for(std::chrono::milliseconds(960));

      REQUIRE(cache->get(i*10 + 1)==11);
      cache->put(i*10 + 3, 33); // update and relocate "3" to more recent partition
      asyik::sleep_for(std::chrono::milliseconds(960));

      REQUIRE(cache->get(i*10 + 1)==11);
      REQUIRE(cache->get(i*10 + 3)==33);
      // 2 should be out of range
      try
      {
        auto k = cache->at(i*10 + 2);

        REQUIRE(false);
      }catch(std::out_of_range &e)
      {
        REQUIRE(true);
      }

      // try erasing an item
      cache->erase(i*10 + 1);
      try
      {
        auto k = cache->at(i*10 + 1);

        REQUIRE(false);
      }catch(std::out_of_range &e)
      {
        REQUIRE(true);
      }    

      num_done++;
  });

  as->execute([&num_done, as]()
  {
    while(num_done<64)
      asyik::sleep_for(std::chrono::milliseconds(10));

    as->stop();
  });

  as->run();
}

}  