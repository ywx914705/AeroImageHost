/*
 * MetricsCollector.hpp - 监控指标采集器头文件
 *
 * 在项目中的作用：定义全局监控指标的采集接口。
 * 所有采集操作使用原子变量，无锁无阻塞，对业务性能零影响。
 */
#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <map>
#include <memory>
#include <vector>
#include <chrono>

class MetricsCollector {
public:
    static MetricsCollector& instance();

    void recordRequest(const std::string& endpoint, double durationMs, int statusCode);
    void recordCacheHit(const std::string& cacheType, bool hit);
    void setMySQLPoolStats(int active, int idle, int waiting);
    void setRedisPoolStats(int active, int idle);
    void recordBytes(bool isUpload, size_t bytes);
    std::string getMetricsJson();
    void tick();
    std::chrono::steady_clock::time_point getStartTime() const { return startTime_; }

private:
    MetricsCollector();

    struct EndpointMetrics {
        std::atomic<int64_t> totalRequests{0};
        std::atomic<int64_t> errorCount{0};
        std::atomic<int64_t> currentWindowRequests{0};
        std::atomic<double> currentWindowQPS{0};
        std::atomic<int64_t> totalDurationUs{0};
        std::atomic<int64_t> lastSecondRequests{0};
    };

    struct LatencySampler {
        std::mutex mutex;
        std::vector<double> samples;
        bool sorted = true;
        static constexpr size_t MAX_SAMPLES = 1000;
        void add(double ms);
        double percentile(double p);
    };

    EndpointMetrics* getOrCreateEndpoint(const std::string& endpoint);

    std::mutex endpointsMutex_;
    std::map<std::string, std::unique_ptr<EndpointMetrics>> endpoints_;

    std::atomic<int64_t> globalCurrentWindowRequests_{0};
    std::atomic<double> globalQPS_{0};

    std::atomic<int64_t> lruHits_{0};
    std::atomic<int64_t> lruMisses_{0};
    std::atomic<int64_t> redisHits_{0};
    std::atomic<int64_t> redisMisses_{0};

    std::atomic<int> mysqlActive_{0};
    std::atomic<int> mysqlIdle_{0};
    std::atomic<int> mysqlWaiting_{0};
    std::atomic<int> redisActive_{0};
    std::atomic<int> redisIdle_{0};

    std::atomic<int64_t> uploadBytes_{0};
    std::atomic<int64_t> uploadCount_{0};
    std::atomic<int64_t> downloadBytes_{0};
    std::atomic<int64_t> downloadCount_{0};

    LatencySampler globalLatency_;

    std::chrono::steady_clock::time_point startTime_;
};
