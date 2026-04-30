#pragma once

#include <hiredis/hiredis.h>
#include <string>
#include <queue>
#include <mutex>
#include <memory>
#include <vector>
#include <unordered_map>

class RedisClient {
public:
    static RedisClient& instance();

    bool init(const std::string& host, int port, int poolSize = 8);
    redisContext* getContext();
    void releaseContext(redisContext* ctx);

    // String operations
    bool set(const std::string& key, const std::string& value);
    std::string get(const std::string& key);
    bool del(const std::string& key);
    bool expire(const std::string& key, int seconds);
    bool exists(const std::string& key);

    // Set operations
    long long sadd(const std::string& key, const std::string& member);
    long long srem(const std::string& key, const std::string& member);
    bool sismember(const std::string& key, const std::string& member);
    std::vector<std::string> smembers(const std::string& key);
    long long scard(const std::string& key);

    // List operations
    long long rpush(const std::string& key, const std::string& value);
    std::vector<std::string> lrange(const std::string& key, int start, int stop);
    long long llen(const std::string& key);
    bool ltrim(const std::string& key, int start, int stop);

    // Hash operations
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    std::string hget(const std::string& key, const std::string& field);
    bool hdel(const std::string& key, const std::string& field);
    bool hexists(const std::string& key, const std::string& field);
    long long hlen(const std::string& key);
    std::unordered_map<std::string, std::string> hgetall(const std::string& key);
    std::vector<std::string> hkeys(const std::string& key);
    std::vector<std::string> hvals(const std::string& key);

    // 批量操作（pipeline）
    std::vector<std::string> multiHget(const std::vector<std::string>& keys, const std::string& field);

private:
    RedisClient() = default;
    std::queue<redisContext*> pool_;
    std::mutex mutex_;
    std::string host_;
    int port_;
    int poolSize_;
};