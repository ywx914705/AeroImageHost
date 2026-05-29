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
#include <shared_mutex>
#include <chrono>

// ========== 本地 LRU 缓存（list + unordered_map 实现真正的 LRU 淘汰） ==========
static const size_t TOKEN_CACHE_CAPACITY = 10000;
static const int TOKEN_CACHE_TTL_SECONDS = 60;
static constexpr int CACHE_SHARDS = 16;

struct CacheEntry {
    int user_id;
    std::chrono::steady_clock::time_point expire_at;
};

using LruList = std::list<std::pair<std::string, CacheEntry>>;

struct TokenShard {
    LruList lruList;
    std::unordered_map<std::string, LruList::iterator> cache;
    std::shared_mutex mutex;
    size_t capacity;

    TokenShard() : capacity(TOKEN_CACHE_CAPACITY / CACHE_SHARDS) {}
};

static TokenShard shards_[CACHE_SHARDS];

static size_t getShard(const std::string& key) {
    size_t h = std::hash<std::string>{}(key);
    return h % CACHE_SHARDS;
}

static void cleanExpiredShard(TokenShard& shard) {
    auto now = std::chrono::steady_clock::now();
    int checkCount = std::min(static_cast<int>(shard.cache.size()), 20);
    auto it = shard.lruList.end();
    for (int i = 0; i < checkCount && it != shard.lruList.begin(); ++i) {
        --it;
        if (it->second.expire_at < now) {
            shard.cache.erase(it->first);
            it = shard.lruList.erase(it);
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
    if (auth_header.size() < 7) return nullptr;
    if (auth_header.compare(0, 7, "Bearer ") != 0 &&
        auth_header.compare(0, 7, "bearer ") != 0) return nullptr;
    std::string token = auth_header.substr(7);
    if (token.empty()) return nullptr;

    size_t si = getShard(token);
    auto& shard = shards_[si];

    {
        std::shared_lock<std::shared_mutex> lock(shard.mutex);
        auto it = shard.cache.find(token);
        if (it != shard.cache.end()) {
            auto now = std::chrono::steady_clock::now();
            if (it->second->second.expire_at > now) {
                auto user = std::make_shared<UserInfo>();
                user->user_id = it->second->second.user_id;
                it->second->second.expire_at = now + std::chrono::seconds(TOKEN_CACHE_TTL_SECONDS);
                lock.unlock();
                {
                    std::unique_lock<std::shared_mutex> wlock(shard.mutex);
                    shard.lruList.splice(shard.lruList.begin(), shard.lruList, shard.cache[token]);
                }
                MetricsCollector::instance().recordCacheHit("lru", true);
                return user;
            }
        }
    }

    {
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        auto it = shard.cache.find(token);
        if (it != shard.cache.end() && it->second->second.expire_at <= std::chrono::steady_clock::now()) {
            shard.lruList.erase(it->second);
            shard.cache.erase(it);
        }
    }

    MetricsCollector::instance().recordCacheHit("lru", false);
    std::string key = "auth_token:" + token;
    RedisClient& redis = RedisClient::instance();
    std::string userIdStr = redis.get(key);

    if (userIdStr.empty()) {
        MetricsCollector::instance().recordCacheHit("redis", false);
        return nullptr;
    }

    try {
        int user_id = std::stoi(userIdStr);
        auto user = std::make_shared<UserInfo>();
        user->user_id = user_id;

        MetricsCollector::instance().recordCacheHit("redis", true);

        static thread_local int expireCounter = 0;
        if (++expireCounter % 100 == 0) {
            redis.expire(key, 86400);
        }

        {
            std::unique_lock<std::shared_mutex> lock(shard.mutex);
            if (shard.cache.size() >= shard.capacity) {
                cleanExpiredShard(shard);
            }
            if (shard.cache.size() >= shard.capacity) {
                auto& lru_back = shard.lruList.back();
                shard.cache.erase(lru_back.first);
                shard.lruList.pop_back();
            }
            shard.lruList.emplace_front(token, CacheEntry{user_id,
                std::chrono::steady_clock::now() + std::chrono::seconds(TOKEN_CACHE_TTL_SECONDS)});
            shard.cache[token] = shard.lruList.begin();
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

    {
        size_t si = getShard(token);
        auto& shard = shards_[si];
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        shard.lruList.emplace_front(token, CacheEntry{user_id,
            std::chrono::steady_clock::now() + std::chrono::seconds(TOKEN_CACHE_TTL_SECONDS)});
        shard.cache[token] = shard.lruList.begin();
    }

    return token;
}

void Auth::revokeToken(const std::string& token) {
    std::string key = "auth_token:" + token;
    RedisClient::instance().del(key);

    size_t si = getShard(token);
    auto& shard = shards_[si];
    std::unique_lock<std::shared_mutex> lock(shard.mutex);
    auto it = shard.cache.find(token);
    if (it != shard.cache.end()) {
        shard.lruList.erase(it->second);
        shard.cache.erase(it);
    }
}
