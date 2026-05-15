/*
 * RateLimiter 模块 - 基于 Redis 的频率限制器实现
 *
 * 职责：通过 Redis 记录操作失败次数，实现滑动窗口频率限制。
 *
 * 核心逻辑：
 *   - isAllowed(): 检查计数器是否未超过阈值
 *   - recordFailure(): 递增计数器并刷新过期时间（滑动窗口）
 *   - reset(): 登录成功后清除计数器
 *   - getRemainingAttempts(): 返回剩余可用尝试次数
 *
 * Redis key 格式："ratelimit:<原始key>"（如 "ratelimit:login:testuser"）
 *
 * 在项目中的作用：
 *   - 登录接口防止暴力破解（默认 5 次失败/15 分钟窗口）
 */
// 基于 Redis 的通用频率限制器
// 原理：用 Redis 记录某个 key 在时间窗口内的失败次数，超过阈值则拒绝
#include "RateLimiter.hpp"
#include "RedisClient.hpp"
#include "Log.hpp"
#include <hiredis/hiredis.h>
#include <sstream>

// 检查某个 key 是否在允许范围内（未超过最大尝试次数）
bool RateLimiter::isAllowed(const std::string& key, int maxAttempts, int windowSeconds) {
    std::string redisKey = "ratelimit:" + key;
    RedisClient& redis = RedisClient::instance();

    std::string countStr = redis.get(redisKey);
    if (countStr.empty()) {
        return true; // 无记录 = 允许
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

// 记录一次失败尝试，使用 Lua 脚本保证原子递增+过期设置
void RateLimiter::recordFailure(const std::string& key, int windowSeconds) {
    std::string redisKey = "ratelimit:" + key;
    RedisClient& redis = RedisClient::instance();

    // 使用 incrWithExpire 保证 INCR + EXPIRE 原子执行
    // 首次递增时自动设置过期时间，后续递增不重置 TTL
    redis.incrWithExpire(redisKey, windowSeconds);
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

bool RateLimiter::allowConsume(const std::string& key, int maxPerWindow, int windowSeconds) {
    std::string redisKey = "ratelimit:consume:" + key;
    long long v = RedisClient::instance().incrWithExpire(redisKey, windowSeconds);
    if (v < 0) {
        return true;
    }
    return v <= static_cast<long long>(maxPerWindow);
}
