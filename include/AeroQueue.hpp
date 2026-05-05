#include <new>
/*
AeroQueue是AeroImageHost项目中一个轻量级的异步任务队列,
它的主要作用是将可能阻塞的耗时操作(如数据库查询、Redis操作、文件 I/O 等)
可以和MySQL配合使用
从Reactor 线程中分离出来,放到独立的后台线程池中执行,从而避免阻塞事件循环,
保证服务器的高响应性。
*/
#pragma once
#include <atomic>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#ifdef U
#pragma push_macro("U")
#undef U
#endif
#include "concurrentqueue.hpp"
#ifdef U
#pragma pop_macro("U")
#endif

class AeroQueue {
public:
    using Task = std::function<void()>;

    static AeroQueue& instance();

    void start(size_t threadCount = 4);
    void stop(bool wait = true);
    void post(Task task);
    ~AeroQueue();

private:
    AeroQueue() = default;
    void workerThread();

    std::vector<std::thread> threads_;
    moodycamel::ConcurrentQueue<Task> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stopped_{false};
};
