/*
 * RateLimiter.hpp - 基于 Redis 的频率限制器头文件
 *
 * 职责：限制某个 key 在时间窗口内的操作次数，防止暴力破解等攻击。
 *
 * 原理：在 Redis 中记录失败次数（key 格式为 "ratelimit:<原始key>"），
 *       超过阈值则拒绝操作。
 *
 * 在项目中的作用：
 *   - 登录接口：限制同一账号的登录失败次数（默认 5 次/15 分钟）
 *   - key 格式为 "ratelimit:login:<account>"
 *
 * 设计：静态方法，无状态。每次失败递增计数器并刷新过期时间（滑动窗口）。
 */
#pragma once
#include <string>

class RateLimiter {
public:
    // 检查是否允许操作：计数器未超过阈值返回 true，超过返回 false
    static bool isAllowed(const std::string& key, int maxAttempts, int windowSeconds);
    static bool checkAndRecord(const std::string& key, int maxAttempts, int windowSeconds);
    static void recordFailure(const std::string& key, int windowSeconds);
    // 重置计数器（登录成功后调用，清除 Redis 中的记录）
    static void reset(const std::string& key);
    // 获取剩余可用尝试次数
    static int getRemainingAttempts(const std::string& key, int maxAttempts, int windowSeconds);

    // 原子递增计数；超过 maxPerWindow 返回 false（用于发信等「每次请求计一次」场景）
    static bool allowConsume(const std::string& key, int maxPerWindow, int windowSeconds);
};
