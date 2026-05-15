/*
 * Auth 模块 - Token 认证实现
 *
 * 核心流程：
 *   1. 用户登录 → generateToken() → 生成 32 位随机字符串 → 存入 Redis（auth_token:<token> = user_id，TTL=24h）
 *   2. API 请求 → verify() → 先查本地 LRU 缓存 → 未命中则查 Redis → 返回 UserInfo
 *   3. 登出 → revokeToken() → 从 Redis 和本地缓存中删除 token
 */
#include "Auth.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include "RedisClient.hpp"
#include "MetricsCollector.hpp"
#include <random>
#include <algorithm>
#include <unordered_map>
#include <list>
#include <mutex>
#include <chrono>

// ========== 本地 LRU 缓存（list + unordered_map 实现真正的 LRU 淘汰） ==========
static const size_t TOKEN_CACHE_CAPACITY = 10000;
static const int TOKEN_CACHE_TTL_SECONDS = 60;

struct CacheEntry {
    int user_id;
    std::chrono::steady_clock::time_point expire_at;
};

// LRU 缓存：list 前端 = 最近使用，后端 = 最久未使用
using LruList = std::list<std::pair<std::string, CacheEntry>>;
static LruList lruList_;
static std::unordered_map<std::string, LruList::iterator> tokenCache_;
static std::mutex cacheMutex_;

// 采样清理：随机检查部分条目，移除过期项（避免全表扫描阻塞）
static void cleanExpiredCache() {
    auto now = std::chrono::steady_clock::now();
    int checkCount = std::min(static_cast<int>(tokenCache_.size()), 100);
    auto it = lruList_.end();
    for (int i = 0; i < checkCount && it != lruList_.begin(); ++i) {
        --it;
        if (it->second.expire_at < now) {
            tokenCache_.erase(it->first);
            it = lruList_.erase(it);
        }
    }
}

static std::string generateRandomString(size_t length) {
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    return result;
}

std::shared_ptr<UserInfo> Auth::verify(const std::string& auth_header) {
    std::string auth = auth_header;
    if (auth.empty()) return nullptr;

    if (auth.size() < 7 || (auth.substr(0, 7) != "Bearer " && auth.substr(0, 7) != "bearer ")) {
        return nullptr;
    }
    std::string token = auth.substr(7);
    token.erase(std::remove_if(token.begin(), token.end(), ::isspace), token.end());
    if (token.empty()) return nullptr;

    // 先查本地 LRU 缓存
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = tokenCache_.find(token);
        if (it != tokenCache_.end()) {
            auto now = std::chrono::steady_clock::now();
            if (it->second->second.expire_at > now) {
                auto user = std::make_shared<UserInfo>();
                user->user_id = it->second->second.user_id;
                // 命中：移到链表头部（最近使用），滑动过期
                it->second->second.expire_at = now + std::chrono::seconds(TOKEN_CACHE_TTL_SECONDS);
                lruList_.splice(lruList_.begin(), lruList_, it->second);
                MetricsCollector::instance().recordCacheHit("lru", true);
                return user;
            }
            // 已过期，移除
            lruList_.erase(it->second);
            tokenCache_.erase(it);
        }
    }

    // 本地缓存未命中，查 Redis
    MetricsCollector::instance().recordCacheHit("lru", false);
    std::string key = "auth_token:" + token;
    RedisClient& redis = RedisClient::instance();
    std::string userIdStr = redis.get(key);

    userIdStr.erase(std::remove_if(userIdStr.begin(), userIdStr.end(), ::isspace), userIdStr.end());
    if (userIdStr.empty()) {
        MetricsCollector::instance().recordCacheHit("redis", false);
        return nullptr;
    }

    try {
        int user_id = std::stoi(userIdStr);
        auto user = std::make_shared<UserInfo>();
        user->user_id = user_id;

        MetricsCollector::instance().recordCacheHit("redis", true);

        // 滑动过期：刷新 Redis TTL
        redis.expire(key, 86400);

        // 写入本地缓存
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            // 超过容量时清理过期条目
            if (tokenCache_.size() >= TOKEN_CACHE_CAPACITY) {
                cleanExpiredCache();
            }
            // 仍超容量，淘汰链表尾部（最久未使用）
            if (tokenCache_.size() >= TOKEN_CACHE_CAPACITY) {
                auto& lru_back = lruList_.back();
                tokenCache_.erase(lru_back.first);
                lruList_.pop_back();
            }
            // 插入到链表头部（最近使用）
            lruList_.emplace_front(token, CacheEntry{user_id,
                std::chrono::steady_clock::now() + std::chrono::seconds(TOKEN_CACHE_TTL_SECONDS)});
            tokenCache_[token] = lruList_.begin();
        }

        return user;
    } catch (const std::exception& e) {
        LOG_ERROR("[Auth] Failed to parse user ID from Redis: " + std::string(e.what()));
        return nullptr;
    }
}

std::string Auth::generateToken(int user_id, const std::string& /* username */) {
    std::string token = generateRandomString(32);
    std::string key = "auth_token:" + token;
    RedisClient& redis = RedisClient::instance();

    if (!redis.setex(key, std::to_string(user_id), 86400)) {
        LOG_ERROR("Failed to store auth token in Redis");
        return "";
    }

    // 同时写入本地缓存（插入头部）
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        lruList_.emplace_front(token, CacheEntry{user_id,
            std::chrono::steady_clock::now() + std::chrono::seconds(TOKEN_CACHE_TTL_SECONDS)});
        tokenCache_[token] = lruList_.begin();
    }

    return token;
}

void Auth::revokeToken(const std::string& token) {
    std::string key = "auth_token:" + token;
    RedisClient::instance().del(key);

    // 同时删除本地缓存
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = tokenCache_.find(token);
    if (it != tokenCache_.end()) {
        lruList_.erase(it->second);
        tokenCache_.erase(it);
    }
}
