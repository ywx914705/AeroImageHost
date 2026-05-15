/*
 * AeroQueue.cc - 异步任务队列实现
 *
 * 在项目中的作用：提供异步任务执行能力，将耗时操作（MinIO上传、文件删除等）从 Drogon 工作线程卸载。
 * 核心机制：基于 moodycamel::ConcurrentQueue（无锁队列）+ 条件变量，4 个工作线程消费任务。
 * 使用方式：AeroQueue::instance().post(lambda) 投递任务，工作线程异步执行。
 * 典型场景：直接上传、预签名确认、分片合并/清理、批量删除等 MinIO I/O 密集型操作。
 */
#include "AeroQueue.hpp"
#include "Log.hpp"
#include <iostream>
#include <chrono>

AeroQueue& AeroQueue::instance() {
    static AeroQueue queue;
    return queue;
}

// 启动指定数量的工作线程
void AeroQueue::start(size_t threadCount) {
    stopped_ = false;
    for (size_t i = 0; i < threadCount; ++i) {
        threads_.emplace_back(&AeroQueue::workerThread, this);
    }
    LOG_INFO("[AeroQueue] 已启动 " + std::to_string(threadCount) + " 个工作线程");
}

void AeroQueue::stop(bool wait) {
    stopped_ = true;
    cv_.notify_all(); // 唤醒所有等待中的工作线程
    if (wait) {
        for (auto& t : threads_) {
            if (t.joinable()) t.join();
        }
    }
}

// 提交任务到队列，同时唤醒一个等待中的工作线程
void AeroQueue::post(Task task) {
    tasks_.enqueue(std::move(task));
    cv_.notify_one();
}

// 工作线程主循环：用条件变量等待任务，避免忙等待浪费 CPU
void AeroQueue::workerThread() {
    while (!stopped_) {
        Task task;
        if (tasks_.try_dequeue(task)) {
            total_tasks_++;
            try {
                task();
            } catch (const std::exception& e) {
                failed_tasks_++;
                LOG_ERROR("[AeroQueue] 任务异常: " + std::string(e.what()));
            } catch (...) {
                failed_tasks_++;
                LOG_ERROR("[AeroQueue] 未知异常");
            }
        } else {
            // 队列为空时用条件变量等待，有新任务时被唤醒
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return tasks_.size_approx() > 0 || stopped_;
            });
        }
    }
}

AeroQueue::~AeroQueue() {
    stop(true);
}
