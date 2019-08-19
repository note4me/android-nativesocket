#ifndef TESTPROJ_THREADPOOL_H
#define TESTPROJ_THREADPOOL_H

#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <deque>
#include <condition_variable>
#include <atomic>

namespace stdx
{
    class ThreadPool
    {
    private:
        std::size_t m_pool_size;
        std::size_t m_pool_size_max_growth;
        enum
        {
            DEFAULT_THREAD_POOL_COUNT = 5,
            DEFAULT_THREAD_MAX_COUNT = 16,
            DEFAULT_THREAD_POOL_COUNT_THRESHOLD = 128
        };
        std::vector<std::thread> m_threads;
        std::deque<std::function<void()> > m_threadfuncs;

        std::condition_variable m_cv;
        std::mutex m_mutex;

        std::atomic_int m_atmoic_working_couter;
        std::atomic_bool m_atmoic_quit_sign;

        std::atomic_bool m_atmoic_fifo_lifo;

        using LOCK = std::unique_lock<std::mutex>;

        void init()
        {
            for (int i = 0; i < m_pool_size; ++i)
            {
                pushback_thread();
            }
        }

        //should invoked in lockguard
        void pushback_thread()
        {
            m_threads.emplace_back([this]() -> void
                                   {
                                       while (!m_atmoic_quit_sign.load())
                                       {
                                           std::function<void()> fn;
                                           try
                                           {
                                               LOCK lock(m_mutex);
                                               m_cv.wait(lock,
                                                         [this]() -> bool
                                                         {
                                                             return !m_threadfuncs.empty() ||
                                                                    m_atmoic_quit_sign.load();
                                                         }
                                               );
                                               if (m_atmoic_quit_sign.load())
                                               {
                                                   break;
                                               }
                                               if (!m_threadfuncs.empty())
                                               {
                                                   if (m_atmoic_fifo_lifo.load())
                                                   {
                                                       fn = std::move(m_threadfuncs.front());
                                                       m_threadfuncs.pop_front();
                                                   }
                                                   else
                                                   {
                                                       fn = std::move(m_threadfuncs.back());
                                                       m_threadfuncs.pop_back();
                                                   }

                                               }
                                               lock.unlock();
                                               ++m_atmoic_working_couter;
                                               if (fn)
                                               {
                                                   fn();
                                               }
                                               --m_atmoic_working_couter;
                                           }
                                           catch (...)
                                           {
                                               std::cout << "catch exp:" << __LINE__ << std::endl;
                                           }

                                       }
                                   });
        }

        void uninit()
        {
            for (auto &thread : m_threads)
            {
                if (thread.joinable())
                {
                    thread.join();
                }
            }
            m_threads.clear();
        }

        //should invoked in lockguard
        bool has_idle()
        {
            return m_atmoic_working_couter.load() < m_threads.size();
        }

    public:
        ThreadPool() : ThreadPool(DEFAULT_THREAD_POOL_COUNT, DEFAULT_THREAD_MAX_COUNT)
        {

        }

        ThreadPool(std::size_t count_begin, std::size_t count_max_growth) : m_pool_size(count_begin),
                                                                            m_pool_size_max_growth(count_max_growth),
                                                                            m_atmoic_working_couter(0),
                                                                            m_atmoic_quit_sign(false),
                                                                            m_atmoic_fifo_lifo(true)
        {
            if (m_pool_size > DEFAULT_THREAD_POOL_COUNT_THRESHOLD)
            {
                m_pool_size = DEFAULT_THREAD_POOL_COUNT_THRESHOLD;
            }
            if (m_pool_size_max_growth > DEFAULT_THREAD_POOL_COUNT_THRESHOLD)
            {
                m_pool_size_max_growth = DEFAULT_THREAD_POOL_COUNT_THRESHOLD;
            }
            init();
        }

        ~ThreadPool()
        {
            quit();
            uninit();
        }

        ThreadPool(const ThreadPool &) = delete;

        ThreadPool(ThreadPool &&) = delete;

        void operator=(const ThreadPool &) = delete;

        void operator=(ThreadPool &&) = delete;
        ///////////////////////////////////////////////////////////////

        template<typename Fn, typename ...Params>
        void commit(Fn &&fn, Params &&... params)
        {
            if (m_atmoic_quit_sign.load())
            {
                return;
            }
            try
            {
                LOCK lock(m_mutex);

                if (!m_atmoic_quit_sign.load())
                {
                    if (!has_idle() && m_threads.size() < m_pool_size_max_growth)
                    {
                        pushback_thread();
                    }

                    m_threadfuncs.emplace_back(
                            std::bind(std::forward<Fn>(fn), std::forward<Params>(params)...)
                    );
                    m_cv.notify_one();
                }

                lock.unlock();
            }
            catch (...)
            {
                std::cout << "catch exp:" << __LINE__ << std::endl;
            }
        }

        void quit()
        {
            m_atmoic_quit_sign.store(true);
            try
            {
                LOCK lock(m_mutex);
                m_cv.notify_all();
            }
            catch (...)
            {
                std::cout << "catch exp:" << __LINE__ << std::endl;
            }
        }

        std::size_t get_pending_count()
        {
            std::size_t count = 0;
            try
            {
                LOCK lock(m_mutex);
                count = m_threadfuncs.size();
            }
            catch (...)
            {
                std::cout << "catch exp:" << __LINE__ << std::endl;
            }

            return count;
        }

        std::size_t get_working_thread_count()
        {
            std::size_t count = 0;
            try
            {
                LOCK lock(m_mutex);
                count = m_threads.size();
            }
            catch (...)
            {
                std::cout << "catch exp:" << __LINE__ << std::endl;
            }

            return count;
        }

        int get_working_count()
        {
            return m_atmoic_working_couter.load();
        }

        void set_execute_fifo_lifo(bool b)
        {
            m_atmoic_fifo_lifo.store(b);
        }


    };
}


class ThreadPoolTest
{
#define SLEEP(x) std::this_thread::sleep_for(x)
    using TIMESECONDS = std::chrono::seconds;
    using TIMEMILLI = std::chrono::milliseconds;
public:
    static void callback(std::string str, float f, const char *sztmp)
    {
        std::cout << "commit2:" << str << " " << f << sztmp << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    struct functor
    {
        void operator()()
        {
            std::cout << "commit3:" << "nothing" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    };

    class RValuePerfectForwardTest
    {
    public:
        int *m_value = nullptr;

        explicit RValuePerfectForwardTest(int value)
        {
            std::cout << "RValuePerfectForwardTest(int)" << std::endl;
            m_value = new int(value);
        }

        ~RValuePerfectForwardTest()
        {
            std::cout << "~RValuePerfectForwardTest" << std::endl;
            uninit();
        }

        RValuePerfectForwardTest(const RValuePerfectForwardTest &obj)
        {
            std::cout << "RValuePerfectForwardTest(&)" << std::endl;
            uninit();
            m_value = new int(*obj.m_value);
        }

        RValuePerfectForwardTest(RValuePerfectForwardTest &&obj) noexcept
        {
            std::cout << "RValuePerfectForwardTest(&&)" << std::endl;
            m_value = obj.m_value;
            obj.m_value = nullptr;
        }

    private:
        void uninit()
        {
            if (m_value)
            {
                std::cout << "delete" << std::endl;
                delete m_value;
                m_value = nullptr;
            }
            else
            {
                std::cout << "NO delete" << std::endl;
            }
        }

    };

    template<typename CALLBACK>
    static void thread_callback(int a, CALLBACK fn)
    {
        std::cout << "commit 5:" << a << std::endl;
        fn();
    }

    class AnotherClass
    {
    public:
        void func()
        {
            std::cout<<"xxxx"<<std::endl;
        }
    };

    static void execute()
    {
//        AnotherClass objtest;
//        std::thread ttttt(std::bind(&AnotherClass::func, objtest));
//        ttttt.detach();


        stdx::ThreadPool thread_pool(1, 5);
//        thread_pool.set_execute_fifo_lifo(false);

//        thread_pool.commit(
//                [](int a, char b) -> int
//                {
//                    std::cout << "commit1:" << a << " " << b << std::endl;
//                    SLEEP(TIMESECONDS(3));
//                    return 12;
//                },
//                123,
//                'c'
//        );

//        SLEEP(TIMEMILLI (10));
//
//        thread_pool.commit(
//                callback,
//                std::string("456"),
//                1.1f,
//                "789"
//        );

//        thread_pool.commit(
//                functor()
//        );

//        RValuePerfectForwardTest xxxxxx(22222);
        thread_pool.commit(
                [](const RValuePerfectForwardTest &test)
                {
                    std::cout << "commit4:" << test.m_value << std::endl;
                },
//                std::cref<RValuePerfectForwardTest>(xxxxxx)
                RValuePerfectForwardTest(11111)
        );

//        auto lambdacallback = []() -> void
//        {
//            std::cout << "thread rans end" << std::endl;
//        };
//        thread_pool.commit(thread_callback<decltype(lambdacallback)>,
//                           999,
//                           lambdacallback
//        );

//        for (int i = 0; i < 10; ++i)
//        {
//            thread_pool.commit(
//                    [=]
//                    {
//                        std::cout << "commitn:" << i << std::endl;
//                        if (i == 9)
//                        {
//                            SLEEP(TIMESECONDS(0));
//                        }
//                    }
//            );
//        }

        SLEEP(TIMESECONDS(1));

        std::cout << "pending count:" << thread_pool.get_pending_count() << std::endl;

        SLEEP(TIMESECONDS(3));
        std::cout << "working count:" << thread_pool.get_working_count() << std::endl;

        SLEEP(TIMESECONDS(5));
    }
};

#endif //TESTPROJ_THREADPOOL_H
