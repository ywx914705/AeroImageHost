// 基于 Redis 的通用频率限制器
// 原理：用 Redis 记录某个 key 在时间窗口内的失败次数，超过阈值则拒绝
#include "RateLimiter.hpp"
#include "RedisClient.hpp"
#include "Log.hpp"
#include <sstream>

// 检查某个 key 是否在允许范围内（未超过最大尝试次数）
bool RateLimiter::isAllowed(const std::string& key, int maxAttempts, int windowSeconds) {
    std::string redisKey = "ratelimit:" + key;
    RedisClient& redis = RedisClient::instance();

    std::string countStr = redis.get(redisKey);
    if (countStr.empty()) {
        return true; // No record = allowed
    }

    int count = 0;
    try {
        count = std::stoi(countStr);
    } catch (...) {
        return true;
    }

    if (count >= maxAttempts) {
        LOG_WARN("[RateLimiter] Rate limit exceeded for key: " + key);
        return false;
    }
    return true;
}

// 记录一次失败尝试，计数器到期自动过期
void RateLimiter::recordFailure(const std::string& key, int windowSeconds) {
    std::string redisKey = "ratelimit:" + key;
    RedisClient& redis = RedisClient::instance();

    // INCR creates the key if it doesn't exist
    std::string countStr = redis.get(redisKey);
    if (countStr.empty()) {
        redis.set(redisKey, "1");
        redis.expire(redisKey, windowSeconds);
    } else {
        // Increment manually since we don't have INCR
        int count = 0;
        try { count = std::stoi(countStr); } catch (...) {}
        count++;
        redis.set(redisKey, std::to_string(count));
        // Only set expiry on first failure (when count was 1 after increment)
        if (count == 1) {
            redis.expire(redisKey, windowSeconds);
        }
    }
}

// 重置计数器（登录成功后调用）
void RateLimiter::reset(const std::string& key) {
    std::string redisKey = "ratelimit:" + key;
    RedisClient::instance().del(redisKey);
}

// 获取剩余可用尝试次数
int RateLimiter::getRemainingAttempts(const std::string& key, int maxAttempts, int windowSeconds) {
    std::string redisKey = "ratelimit:" + key;
    std::string countStr = RedisClient::instance().get(redisKey);
    if (countStr.empty()) return maxAttempts;

    int count = 0;
    try { count = std::stoi(countStr); } catch (...) {}
    int remaining = maxAttempts - count;
    return remaining > 0 ? remaining : 0;
}
