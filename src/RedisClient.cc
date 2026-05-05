// Redis 连接池实现
// 提供连接复用、超时等待、健康检查、自动重建等机制
#include "RedisClient.hpp"
#include "Log.hpp"
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>

RedisClient& RedisClient::instance() {
    static RedisClient instance;
    return instance;
}

// 初始化连接池：创建指定数量的 Redis 连接
bool RedisClient::init(const std::string& host, int port, int poolSize) {
    host_ = host;
    port_ = port;
    poolSize_ = poolSize;

    for (int i = 0; i < poolSize_; ++i) {
        redisContext* ctx = redisConnect(host_.c_str(), port_);
        if (!ctx || ctx->err) {
            if (ctx) {
                LOG_ERROR("[Redis] Connection error: " + std::string(ctx->errstr));
                redisFree(ctx);
            } else {
                LOG_ERROR("[Redis] Cannot allocate redis context");
            }
            return false;
        }
        pool_.push(ctx);
    }
    LOG_INFO("[Redis] 连接池初始化完成，创建 " + std::to_string(poolSize_) + " 个连接");
    return true;
}

// 从连接池获取一个可用连接（带超时等待 + 健康检查 + 自动重建）
redisContext* RedisClient::getContext() {
    std::unique_lock<std::mutex> lock(mutex_);

    // Wait up to 3 seconds for a connection to become available
    if (!cv_.wait_for(lock, std::chrono::seconds(3), [this]() { return !pool_.empty(); })) {
        LOG_ERROR("[Redis] 获取连接超时（3秒）");
        return nullptr;
    }

    redisContext* ctx = pool_.front();
    pool_.pop();

    // 快速健康检查：如果连接已断开，尝试重建
    redisReply* pingReply = (redisReply*)redisCommand(ctx, "PING");
    if (!pingReply || pingReply->type != REDIS_REPLY_STATUS) {
        if (pingReply) freeReplyObject(pingReply);
        LOG_WARN("[Redis] 连接失效，尝试重建...");
        redisFree(ctx);
        ctx = redisConnect(host_.c_str(), port_);
        if (!ctx || ctx->err) {
            if (ctx) {
                LOG_ERROR("[Redis] 重建连接失败: " + std::string(ctx->errstr));
                redisFree(ctx);
            }
            return nullptr;
        }
    } else {
        freeReplyObject(pingReply);
    }

    return ctx;
}

// 归还连接到连接池，唤醒等待中的线程
void RedisClient::releaseContext(redisContext* ctx) {
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(ctx);
    cv_.notify_one();
}

bool RedisClient::set(const std::string& key, const std::string& value) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SET %s %s", key.c_str(), value.c_str());
    bool ok = (reply && reply->type != REDIS_REPLY_ERROR);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

std::string RedisClient::get(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return "";
    redisReply* reply = (redisReply*)redisCommand(ctx, "GET %s", key.c_str());
    std::string val;
    if (reply && reply->type == REDIS_REPLY_STRING) {
        val = std::string(reply->str, reply->len);
    }
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return val;
}

bool RedisClient::del(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());
    bool ok = (reply && reply->integer > 0);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

bool RedisClient::expire(const std::string& key, int seconds) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "EXPIRE %s %d", key.c_str(), seconds);
    bool ok = (reply && reply->integer == 1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

bool RedisClient::exists(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "EXISTS %s", key.c_str());
    bool ok = (reply && reply->integer > 0);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

long long RedisClient::sadd(const std::string& key, const std::string& member) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SADD %s %s", key.c_str(), member.c_str());
    long long ret = (reply ? reply->integer : -1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

long long RedisClient::srem(const std::string& key, const std::string& member) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SREM %s %s", key.c_str(), member.c_str());
    long long ret = (reply ? reply->integer : -1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

bool RedisClient::sismember(const std::string& key, const std::string& member) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SISMEMBER %s %s", key.c_str(), member.c_str());
    bool ok = (reply && reply->integer == 1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

std::vector<std::string> RedisClient::smembers(const std::string& key) {
    redisContext* ctx = getContext();
    std::vector<std::string> result;
    if (!ctx) return result;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SMEMBERS %s", key.c_str());
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            result.emplace_back(reply->element[i]->str, reply->element[i]->len);
        }
    }
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return result;
}

long long RedisClient::scard(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SCARD %s", key.c_str());
    long long ret = (reply ? reply->integer : -1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

long long RedisClient::rpush(const std::string& key, const std::string& value) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "RPUSH %s %s", key.c_str(), value.c_str());
    long long ret = (reply ? reply->integer : -1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

std::vector<std::string> RedisClient::lrange(const std::string& key, int start, int stop) {
    redisContext* ctx = getContext();
    std::vector<std::string> result;
    if (!ctx) return result;
    redisReply* reply = (redisReply*)redisCommand(ctx, "LRANGE %s %d %d", key.c_str(), start, stop);
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            result.emplace_back(reply->element[i]->str, reply->element[i]->len);
        }
    }
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return result;
}

long long RedisClient::llen(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "LLEN %s", key.c_str());
    long long ret = (reply ? reply->integer : -1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

bool RedisClient::ltrim(const std::string& key, int start, int stop) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "LTRIM %s %d %d", key.c_str(), start, stop);
    bool ok = (reply && reply->type != REDIS_REPLY_ERROR);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

// ==================== 哈希操作 ====================
bool RedisClient::hset(const std::string& key, const std::string& field, const std::string& value) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
    bool ok = (reply && reply->type != REDIS_REPLY_ERROR);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

std::string RedisClient::hget(const std::string& key, const std::string& field) {
    redisContext* ctx = getContext();
    if (!ctx) return "";
    redisReply* reply = (redisReply*)redisCommand(ctx, "HGET %s %s", key.c_str(), field.c_str());
    std::string val;
    if (reply && reply->type == REDIS_REPLY_STRING) {
        val = std::string(reply->str, reply->len);
    }
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return val;
}

bool RedisClient::hdel(const std::string& key, const std::string& field) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HDEL %s %s", key.c_str(), field.c_str());
    bool ok = (reply && reply->integer > 0);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

bool RedisClient::hexists(const std::string& key, const std::string& field) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HEXISTS %s %s", key.c_str(), field.c_str());
    bool ok = (reply && reply->integer == 1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

long long RedisClient::hlen(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HLEN %s", key.c_str());
    long long ret = (reply && reply->type == REDIS_REPLY_INTEGER) ? reply->integer : -1;
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

std::unordered_map<std::string, std::string> RedisClient::hgetall(const std::string& key) {
    redisContext* ctx = getContext();
    std::unordered_map<std::string, std::string> result;
    if (!ctx) return result;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HGETALL %s", key.c_str());
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; i += 2) {
            std::string field(reply->element[i]->str, reply->element[i]->len);
            std::string value(reply->element[i+1]->str, reply->element[i+1]->len);
            result[field] = value;
        }
    }
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return result;
}

std::vector<std::string> RedisClient::hkeys(const std::string& key) {
    redisContext* ctx = getContext();
    std::vector<std::string> result;
    if (!ctx) return result;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HKEYS %s", key.c_str());
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            result.emplace_back(reply->element[i]->str, reply->element[i]->len);
        }
    }
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return result;
}

std::vector<std::string> RedisClient::hvals(const std::string& key) {
    redisContext* ctx = getContext();
    std::vector<std::string> result;
    if (!ctx) return result;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HVALS %s", key.c_str());
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            result.emplace_back(reply->element[i]->str, reply->element[i]->len);
        }
    }
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return result;
}

// ==================== 批量操作 ====================
std::vector<std::string> RedisClient::multiHget(const std::vector<std::string>& keys, const std::string& field) {
    std::vector<std::string> result;
    if (keys.empty()) return result;
    result.resize(keys.size());

    redisContext* ctx = getContext();
    if (!ctx) return result;

    // 使用 pipeline 批量发送命令
    for (const auto& key : keys) {
        redisAppendCommand(ctx, "HGET %s %s", key.c_str(), field.c_str());
    }

    // 获取所有回复
    for (size_t i = 0; i < keys.size(); ++i) {
        redisReply* reply = nullptr;
        if (redisGetReply(ctx, (void**)&reply) == REDIS_OK && reply) {
            if (reply->type == REDIS_REPLY_STRING) {
                result[i] = std::string(reply->str, reply->len);
            }
            freeReplyObject(reply);
        }
    }

    releaseContext(ctx);
    return result;
}