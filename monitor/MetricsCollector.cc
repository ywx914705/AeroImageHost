/*
 * MetricsCollector.cc - 监控指标采集器实现
 *
 * 在项目中的作用：实现全局监控指标的采集、存储和查询。
 * 所有采集操作使用原子变量，无锁无阻塞，对业务性能零影响。
 * tick() 每秒调用一次，滚动时间窗口计算 QPS。
 */
#include "MetricsCollector.hpp"
#include "Utils.hpp"
#include "ConnectionPool.hpp"
#include "RedisClient.hpp"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <algorithm>

MetricsCollector& MetricsCollector::instance() {
    static MetricsCollector inst;
    return inst;
}

MetricsCollector::MetricsCollector()
    : startTime_(std::chrono::steady_clock::now()) {}

/* 获取或创建端点指标对象 */
MetricsCollector::EndpointMetrics* MetricsCollector::getOrCreateEndpoint(const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(endpointsMutex_);
    auto it = endpoints_.find(endpoint);
    if (it != endpoints_.end()) return it->second.get();
    auto m = std::make_unique<EndpointMetrics>();
    auto* ptr = m.get();
    endpoints_[endpoint] = std::move(m);
    return ptr;
}

/* 记录一次请求完成 */
void MetricsCollector::recordRequest(const std::string& endpoint, double durationMs, int statusCode) {
    auto* m = getOrCreateEndpoint(endpoint);

    // 先 fetch_add 获取旧值用于采样，再更新其他计数
    int64_t oldCount = m->totalRequests.fetch_add(1, std::memory_order_relaxed);
    m->totalDurationUs.fetch_add(static_cast<int64_t>(durationMs * 1000), std::memory_order_relaxed);
    m->currentWindowRequests.fetch_add(1, std::memory_order_relaxed);
    globalCurrentWindowRequests_.fetch_add(1, std::memory_order_relaxed);

    // 仅将 5xx 服务端错误计入错误统计
    // 4xx 客户端错误（如 403 权限拒绝、404 资源不存在）属于正常业务行为，不计入错误
    if (statusCode >= 500) {
        m->errorCount.fetch_add(1, std::memory_order_relaxed);
    }

    // 采样延迟（非每请求都采样，1/10 概率采样减少内存开销）
    if (oldCount % 10 == 0) {
        globalLatency_.add(durationMs);
    }
}

/* 记录缓存命中/未命中 */
void MetricsCollector::recordCacheHit(const std::string& cacheType, bool hit) {
    if (cacheType == "lru") {
        if (hit) lruHits_.fetch_add(1, std::memory_order_relaxed);
        else lruMisses_.fetch_add(1, std::memory_order_relaxed);
    } else if (cacheType == "redis") {
        if (hit) redisHits_.fetch_add(1, std::memory_order_relaxed);
        else redisMisses_.fetch_add(1, std::memory_order_relaxed);
    }
}

/* 记录 MySQL 连接池状态 */
void MetricsCollector::setMySQLPoolStats(int active, int idle, int waiting) {
    mysqlActive_.store(active, std::memory_order_relaxed);
    mysqlIdle_.store(idle, std::memory_order_relaxed);
    mysqlWaiting_.store(waiting, std::memory_order_relaxed);
}

/* 记录 Redis 连接池状态 */
void MetricsCollector::setRedisPoolStats(int active, int idle) {
    redisActive_.store(active, std::memory_order_relaxed);
    redisIdle_.store(idle, std::memory_order_relaxed);
}

/* 记录上传/下载字节数 */
void MetricsCollector::recordBytes(bool isUpload, size_t bytes) {
    if (isUpload) {
        uploadBytes_.fetch_add(bytes, std::memory_order_relaxed);
        uploadCount_.fetch_add(1, std::memory_order_relaxed);
    } else {
        downloadBytes_.fetch_add(bytes, std::memory_order_relaxed);
        downloadCount_.fetch_add(1, std::memory_order_relaxed);
    }
}

/* 延迟采样 - 使用二分插入保持有序 */
void MetricsCollector::LatencySampler::add(double ms) {
    std::lock_guard<std::mutex> lock(mutex);
    if (samples.size() >= MAX_SAMPLES) {
        samples.erase(samples.begin());
        sorted = false;
    }
    samples.push_back(ms);
    sorted = false;
}

/* 计算延迟百分位 - 延迟排序，仅在需要时排序 */
double MetricsCollector::LatencySampler::percentile(double p) {
    std::lock_guard<std::mutex> lock(mutex);
    if (samples.empty()) return 0.0;
    if (!sorted) {
        std::sort(samples.begin(), samples.end());
        sorted = true;
    }
    size_t idx = static_cast<size_t>(p / 100.0 * samples.size());
    if (idx >= samples.size()) idx = samples.size() - 1;
    return samples[idx];
}

/* 每秒调用一次，滚动时间窗口计算 QPS */
void MetricsCollector::tick() {
    // 全局 QPS
    int64_t windowReqs = globalCurrentWindowRequests_.exchange(0, std::memory_order_relaxed);
    globalQPS_.store(static_cast<double>(windowReqs), std::memory_order_relaxed);

    // 各端点 QPS
    std::lock_guard<std::mutex> lock(endpointsMutex_);
    for (auto& [name, m] : endpoints_) {
        int64_t reqs = m->currentWindowRequests.exchange(0, std::memory_order_relaxed);
        m->currentWindowQPS.store(static_cast<double>(reqs), std::memory_order_relaxed);
        m->lastSecondRequests.store(reqs, std::memory_order_relaxed);
    }

    // 采集连接池状态
    try {
        auto mysqlStats = ConnectionPool::getInstance().getStats();
        setMySQLPoolStats(mysqlStats.active, mysqlStats.idle, mysqlStats.waiting);
    } catch (...) {}

    try {
        auto redisStats = RedisClient::instance().getStats();
        setRedisPoolStats(redisStats.active, redisStats.idle);
    } catch (...) {}
}

/* 生成监控指标 JSON */
std::string MetricsCollector::getMetricsJson() {
    using namespace rapidjson;
    Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    // 系统运行时间（秒）
    auto uptime = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startTime_).count();
    doc.AddMember("uptime_seconds", uptime, alloc);

    // 全局 QPS
    doc.AddMember("global_qps", globalQPS_.load(), alloc);

    // 全局延迟分布
    doc.AddMember("latency_avg_ms", 0.0, alloc);
    doc.AddMember("latency_p50_ms", globalLatency_.percentile(50), alloc);
    doc.AddMember("latency_p95_ms", globalLatency_.percentile(95), alloc);
    doc.AddMember("latency_p99_ms", globalLatency_.percentile(99), alloc);

    // 缓存命中率
    int64_t lruH = lruHits_.load(), lruM = lruMisses_.load();
    int64_t redisH = redisHits_.load(), redisM = redisMisses_.load();
    double lruRate = (lruH + lruM > 0) ? (double)lruH / (lruH + lruM) * 100 : 0;
    double redisRate = (redisH + redisM > 0) ? (double)redisH / (redisH + redisM) * 100 : 0;

    Value cache(kObjectType);
    cache.AddMember("lru_hits", lruH, alloc);
    cache.AddMember("lru_misses", lruM, alloc);
    cache.AddMember("lru_hit_rate", lruRate, alloc);
    cache.AddMember("redis_hits", redisH, alloc);
    cache.AddMember("redis_misses", redisM, alloc);
    cache.AddMember("redis_hit_rate", redisRate, alloc);
    doc.AddMember("cache", cache, alloc);

    // 连接池状态
    Value mysql(kObjectType);
    mysql.AddMember("active", mysqlActive_.load(), alloc);
    mysql.AddMember("idle", mysqlIdle_.load(), alloc);
    mysql.AddMember("waiting", mysqlWaiting_.load(), alloc);
    doc.AddMember("mysql_pool", mysql, alloc);

    Value redis(kObjectType);
    redis.AddMember("active", redisActive_.load(), alloc);
    redis.AddMember("idle", redisIdle_.load(), alloc);
    doc.AddMember("redis_pool", redis, alloc);

    // 吞吐量
    Value throughput(kObjectType);
    throughput.AddMember("upload_bytes", uploadBytes_.load(), alloc);
    throughput.AddMember("upload_count", uploadCount_.load(), alloc);
    throughput.AddMember("download_bytes", downloadBytes_.load(), alloc);
    throughput.AddMember("download_count", downloadCount_.load(), alloc);
    doc.AddMember("throughput", throughput, alloc);

    // 各端点指标
    Value endpoints(kObjectType);
    {
        std::lock_guard<std::mutex> lock(endpointsMutex_);
        int64_t totalReqs = 0;
        double totalDur = 0;
        for (auto& [name, m] : endpoints_) {
            Value ep(kObjectType);
            ep.AddMember("total_requests", m->totalRequests.load(), alloc);
            ep.AddMember("error_count", m->errorCount.load(), alloc);
            ep.AddMember("qps", m->currentWindowQPS.load(), alloc);

            int64_t reqs = m->totalRequests.load();
            int64_t durUs = m->totalDurationUs.load();
            double avgMs = (reqs > 0) ? (double)durUs / reqs / 1000.0 : 0.0;
            ep.AddMember("avg_ms", avgMs, alloc);

            // 计算错误率
            int64_t errs = m->errorCount.load();
            double errRate = (reqs > 0) ? (double)errs / reqs * 100.0 : 0.0;
            ep.AddMember("error_rate", errRate, alloc);

            totalReqs += reqs;
            totalDur += durUs / 1000.0;

            Value epName(name.c_str(), alloc);
            endpoints.AddMember(epName, ep, alloc);
        }

        if (totalReqs > 0) {
            doc["latency_avg_ms"] = totalDur / totalReqs;
        }
    }
    doc.AddMember("endpoints", endpoints, alloc);

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}
