#pragma once
#include <thread>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <mutex>

class BackgroundService {
public:
    void start(std::atomic<bool>& running);
    void stop();

private:
    std::mutex cleanupMtx_, viewSyncMtx_, metricsMtx_;
    std::condition_variable cleanupCv_, viewSyncCv_, metricsCv_;
    std::vector<std::thread> threads_;
};
