#ifndef LIBASYIK_MEMCACHE_HPP
#define LIBASYIK_MEMCACHE_HPP

#include <list>
#include <memory>
#include <algorithm>
#include <functional>
#include <boost/fiber/all.hpp>
#include "common.hpp"
#include "libasyik/service.hpp"
#include "libasyik/error.hpp"

namespace asyik
{
    void _TEST_invoke_memcache();

    struct single_thread
    {
        using guard_type = int;
        using mutex_type = int;
        template <typename T>
        using atomic_type = T;
    };

    template<typename MutexType>
    struct multi_thread
    {
        using guard_type = std::lock_guard<MutexType>;
        using mutex_type = MutexType;
        template <typename T>
        using atomic_type = std::atomic<T>;
    };

    template <class Key, class T, int expiry, int segments, typename thread_policy>
    class memcache: public std::enable_shared_from_this<memcache<Key, T, expiry, segments, thread_policy>>
    {
        public: 
        struct private_
        {
        };
        public:
        memcache()=delete;
        memcache(const memcache&)=delete;
        memcache &operator=(const memcache&)=delete;
        memcache(struct private_ &&)
        :closed(false)
        {
        }

        ~memcache()
        {

        }

        void put(const Key &k, T && v)
        {
            using namespace std::chrono;
            typename thread_policy::guard_type t(mtx);

            // delete old key
            for(auto & m : map_list)
            {
                m.second.erase(k);
            }

            uint32_t current_ms = duration_cast<std::chrono::milliseconds>(system_clock::now().time_since_epoch()).count();
            uint32_t expiry_at = current_ms + expiry*1000;

            if((map_list.begin()!=map_list.end()) && (map_list.begin()->first >= expiry_at))
            {
                map_list.begin()->second[k]=std::forward<T>(v);
            }else // create new cluster
            {
                std::map<Key, T> m;
                m.emplace(k, std::forward<T>(v));
                map_list[expiry_at+(expiry*1000/segments)] = std::move(m);
            }
        }

        void erase(const Key &k)
        {
            typename thread_policy::guard_type t(mtx);

            for(auto & m : map_list)
            {
                m.second.erase(k);
            }
        }

        void clear()
        {
            typename thread_policy::guard_type t(mtx);

            map_list.clear();
        }

        T& at(const Key& k)
        {
            using namespace std::chrono;
            uint32_t current_ms = duration_cast<std::chrono::milliseconds>(system_clock::now().time_since_epoch()).count();

            typename thread_policy::guard_type t(mtx);
            for(auto & m : map_list)
            {
                if(current_ms>m.first)
                    break;
                if(m.second.count(k) && (m.first>=current_ms))
                {
                    return m.second.at(k);
                }
            }
            throw std::out_of_range("item not found/out of range in memcache!");
        }

        const T& at(const Key& k) const
        {
            return at(k);
        }

        T& get(const Key& k)
        {
            using namespace std::chrono;

            typename thread_policy::guard_type t(mtx);
            prune();

            for(auto & m : map_list)
            {
                if(m.second.count(k))
                {
                    uint32_t current_ms = duration_cast<std::chrono::milliseconds>(system_clock::now().time_since_epoch()).count();
                    uint32_t expiry_at = current_ms + expiry*1000;

                    if((map_list.begin()!=map_list.end()) && (map_list.begin()->first >= expiry_at))
                    {
                        if(map_list.begin()->first!=m.first)
                        {
                            map_list.begin()->second[k]=std::move(m.second.at(k));
                            m.second.erase(k);
                        }
                        return map_list.begin()->second.at(k);
                    }else // create new cluster
                    {
                        std::map<Key, T> c;
                        c.emplace(k, std::move(m.second.at(k)));

                        map_list[expiry_at+(expiry*1000/segments)] = std::move(c);
                        m.second.erase(k);
                        return map_list.at(expiry_at+(expiry*1000/segments)).at(k);
                    }
                }
            }
            throw std::out_of_range("item not found/out of range in memcache!");
        }

        private:
        void prune()
        {
            using namespace std::chrono;
            uint32_t current_ms = duration_cast<std::chrono::milliseconds>(system_clock::now().time_since_epoch()).count();

            for(auto iter=map_list.begin(); iter != map_list.end();) 
            {
                if (current_ms>iter->first)
                    iter = map_list.erase(iter);
                else 
                    ++iter;
            }
        }
        typename thread_policy::template atomic_type<bool> closed;
        std::map<uint32_t, std::map<Key, T>, std::greater<int>> map_list;
        typename thread_policy::mutex_type mtx;

        template <class k, class t, int e, int s>
        friend std::shared_ptr<memcache<k, t, e, s, single_thread>> make_memcache(service_ptr as);
        template <class k, class t, int e, int s>
        friend std::shared_ptr<memcache<k, t, e, s, multi_thread<boost::fibers::mutex>>> make_memcache_mt(service_ptr as);
    };

    template <class Key, class T, int expiry, int segments=10>
    inline std::shared_ptr<memcache<Key, T, expiry, segments, single_thread>> make_memcache(service_ptr as)
    {
        auto cache = std::make_shared<memcache<Key, T, expiry, segments, single_thread>>(
            typename memcache<Key, T, expiry, segments, single_thread>::private_{});
        as->execute([c = std::weak_ptr<memcache<Key, T, expiry, segments, single_thread>>(cache)]()
        {
            while(1)
            {
                std::shared_ptr<memcache<Key, T, expiry, segments, single_thread>> cache;
                asyik::sleep_for(std::chrono::milliseconds((expiry*1000)/segments));
                if((cache = c.lock()))
                {
                    cache->prune();
                    cache.reset();
                }
                else
                    break;
            }
        });
        return cache;
    }

    template <class Key, class T, int expiry, int segments=10>
    inline std::shared_ptr<memcache<Key, T, expiry, segments, multi_thread<boost::fibers::mutex>>> make_memcache_mt(service_ptr as)
    {
        auto cache = std::make_shared<memcache<Key, T, expiry, segments, multi_thread<boost::fibers::mutex>>>(
            typename memcache<Key, T, expiry, segments, multi_thread<boost::fibers::mutex>>::private_{});
        as->async([c = std::weak_ptr<memcache<Key, T, expiry, segments, multi_thread<boost::fibers::mutex>>>(cache)]()
        {
            while(1)
            {
                std::shared_ptr<memcache<Key, T, expiry, segments, multi_thread<boost::fibers::mutex>>> cache;
                asyik::sleep_for(std::chrono::milliseconds((expiry*1000)/segments));
                if((cache = c.lock()))
                {
                    typename multi_thread<boost::fibers::mutex>::guard_type t(cache->mtx);
                        
                    cache->prune();
                    cache.reset();
                }
                else
                    break;
            }
        });
        return cache;
    }
}
#endif
