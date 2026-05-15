/*
 * AeroQueue.hpp - 异步任务队列头文件
 *
 * 在项目中的作用：提供异步任务执行能力，将耗时操作（MinIO上传/删除、文件合并等）
 * 从 Drogon Reactor 线程中分离出来，放到独立的后台线程池中执行，避免阻塞事件循环。
 *
 * 核心机制：基于 moodycamel::ConcurrentQueue（无锁队列），支持多生产者多消费者。
 * 使用方式：AeroQueue::instance().post(lambda) 投递任务，工作线程异步执行。
 */
#pragma once

#include <new>
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

    /* 获取单例实例 */
    static AeroQueue& instance();

    /* 启动工作线程池（默认 4 个线程） */
    void start(size_t threadCount = 4);

    /* 停止工作线程池，wait=true 时等待所有任务完成 */
    void stop(bool wait = true);

    /* 投递异步任务到队列，工作线程会自动取出执行 */
    void post(Task task);

    ~AeroQueue();

private:
    AeroQueue() = default;

    /* 工作线程函数：循环从队列取任务执行 */
    void workerThread();

    std::vector<std::thread> threads_;                  // 工作线程列表
    moodycamel::ConcurrentQueue<Task> tasks_;           // 无锁任务队列
    std::mutex mutex_;                                  // 保护条件变量的互斥锁
    std::condition_variable cv_;                        // 通知工作线程有新任务
    std::atomic<bool> stopped_{false};                  // 是否已停止
    std::atomic<uint64_t> total_tasks_{0};              // 已执行任务总数
    std::atomic<uint64_t> failed_tasks_{0};             // 失败任务总数

public:
    uint64_t totalTasks() const { return total_tasks_.load(); }
    uint64_t failedTasks() const { return failed_tasks_.load(); }
};
