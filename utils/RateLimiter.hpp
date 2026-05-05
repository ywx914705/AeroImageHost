#pragma once
#include <string>

// 基于 Redis 的频率限制器，用于防止暴力破解等攻击
// 原理：在 Redis 中记录某个 key（如 "login:用户名"）的失败次数，超过阈值则拒绝
class RateLimiter {
public:
    // 检查是否允许操作，超过阈值返回 false
    static bool isAllowed(const std::string& key, int maxAttempts, int windowSeconds);

    // 记录一次失败尝试
    static void recordFailure(const std::string& key, int windowSeconds);

    // 重置计数器（登录成功后调用）
    static void reset(const std::string& key);

    // 获取剩余可用尝试次数
    static int getRemainingAttempts(const std::string& key, int maxAttempts, int windowSeconds);
};
