/*
 * RedisClient 模块 - Redis 连接池客户端实现
 *
 * 职责：管理 Redis 连接池，封装常用的 Redis 操作。
 *
 * 核心功能：
 *   - 连接池管理：创建/获取/归还连接，带 3 秒超时等待
 *   - 健康检查：每次获取连接时执行 PING 检查，失效则自动重建
 *   - String 操作：set/get/del/expire/exists
 *   - Set 操作：sadd/srem/sismember/smembers/scard
 *   - List 操作：rpush/lrange/llen/ltrim
 *   - Hash 操作：hset/hget/hdel/hexists/hlen/hgetall/hkeys/hvals
 *   - Pipeline 批量操作：multiHget（减少网络往返）
 *
 * 设计：单例模式，连接池默认 16 个连接。
 */
// Redis 连接池实现
// 提供连接复用、超时等待、健康检查、自动重建等机制
#include "RedisClient.hpp"
#include "Log.hpp"
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>

namespace {

bool redis_auth_if_needed(redisContext* ctx, const std::string& password) {
    if (!ctx || ctx->err) {
        return false;
    }
    if (password.empty()) {
        return true;
    }
    redisReply* reply = (redisReply*)redisCommand(ctx, "AUTH %b", password.data(), password.size());
    bool ok = reply && reply->type == REDIS_REPLY_STATUS && reply->str != nullptr &&
              static_cast<size_t>(reply->len) == 2 && std::memcmp(reply->str, "OK", 2) == 0;
    if (!ok && reply && reply->type == REDIS_REPLY_ERROR && reply->str) {
        LOG_ERROR(std::string("[Redis] AUTH failed: ") + reply->str);
    }
    if (reply) {
        freeReplyObject(reply);
    }
    return ok;
}

}  // namespace

RedisClient& RedisClient::instance() {
    static RedisClient instance;
    return instance;
}

// 初始化连接池：创建指定数量的 Redis 连接
bool RedisClient::init(const std::string& host, int port, int poolSize, const std::string& password) {
    host_ = host;
    port_ = port;
    poolSize_ = poolSize;
    password_ = password;
    stopped_.store(false);

    auto now = std::chrono::steady_clock::now();
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
        if (!redis_auth_if_needed(ctx, password_)) {
            LOG_ERROR("[Redis] AUTH failed during pool initialization");
            redisFree(ctx);
            return false;
        }
        size_t idx = static_cast<size_t>(i % SHARD_COUNT);
        {
            std::lock_guard<std::mutex> lock(shards_[idx].mutex);
            shards_[idx].pool.push({ctx, now});
        }
    }
    LOG_INFO("[Redis] 连接池初始化完成，创建 " + std::to_string(poolSize_) + " 个连接，分片到 " + std::to_string(SHARD_COUNT) + " 个分片");
    return true;
}

// 从连接池获取一个可用连接（带超时等待 + 懒验证 + 自动重建）
redisContext* RedisClient::getContext() {
    size_t idx = getShard();
    auto& shard = shards_[idx];
    std::unique_lock<std::mutex> lock(shard.mutex);

    if (!shard.cv.wait_for(lock, std::chrono::seconds(3), [this, &shard]() {
        return stopped_.load() || !shard.pool.empty();
    })) {
        LOG_ERROR("[Redis] 获取连接超时（3秒）");
        return nullptr;
    }

    if (stopped_.load()) {
        return nullptr;
    }

    auto [ctx, lastCheck] = shard.pool.front();
    shard.pool.pop();

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastCheck).count();
    if (elapsed >= 30) {
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
            if (!redis_auth_if_needed(ctx, password_)) {
                LOG_ERROR("[Redis] AUTH failed after reconnect");
                redisFree(ctx);
                return nullptr;
            }
        } else {
            freeReplyObject(pingReply);
        }
    }

    return ctx;
}

// 归还连接到连接池，记录本次验证时间
void RedisClient::releaseContext(redisContext* ctx) {
    if (!ctx) return;
    size_t idx = getShard();
    auto& shard = shards_[idx];
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.pool.push({ctx, std::chrono::steady_clock::now()});
    }
    shard.cv.notify_one();
}

RedisClient::Stats RedisClient::getStats() {
    Stats s;
    for (int i = 0; i < SHARD_COUNT; ++i) {
        std::lock_guard<std::mutex> lock(shards_[i].mutex);
        s.idle += static_cast<int>(shards_[i].pool.size());
    }
    s.active = poolSize_ - s.idle;
    return s;
}

void RedisClient::close() {
    stopped_.store(true);
    for (int i = 0; i < SHARD_COUNT; ++i) {
        {
            std::lock_guard<std::mutex> lock(shards_[i].mutex);
            while (!shards_[i].pool.empty()) {
                auto [ctx, _] = shards_[i].pool.front();
                shards_[i].pool.pop();
                if (ctx) redisFree(ctx);
            }
        }
        shards_[i].cv.notify_all();
    }
}

size_t RedisClient::getShard() const {
    return std::hash<std::thread::id>{}(std::this_thread::get_id()) % SHARD_COUNT;
}

std::string RedisClient::ping() {
    redisContext* ctx = getContext();
    if (!ctx) return "CONNECTION_FAILED";
    redisReply* reply = (redisReply*)redisCommand(ctx, "PING");
    std::string result;
    if (reply && reply->type == REDIS_REPLY_STATUS) {
        result = reply->str;
    } else {
        result = "ERROR";
    }
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return result;
}

std::string RedisClient::type(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return "none";
    redisReply* reply = (redisReply*)redisCommand(ctx, "TYPE %b", key.data(), key.size());
    std::string result = "none";
    if (reply && reply->type == REDIS_REPLY_STATUS) {
        result = reply->str;
    }
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return result;
}

bool RedisClient::set(const std::string& key, const std::string& value) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SET %b %b", key.data(), key.size(), value.data(), value.size());
    bool ok = (reply && reply->type != REDIS_REPLY_ERROR);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

// 原子操作：设置键值对并指定过期时间（避免 set+expire 之间的竞态窗口）
bool RedisClient::setex(const std::string& key, const std::string& value, int seconds) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SET %b %b EX %d", key.data(), key.size(), value.data(), value.size(), seconds);
    bool ok = (reply && reply->type != REDIS_REPLY_ERROR);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

std::string RedisClient::get(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return "";
    redisReply* reply = (redisReply*)redisCommand(ctx, "GET %b", key.data(), key.size());
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
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %b", key.data(), key.size());
    bool ok = (reply && reply->integer > 0);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

bool RedisClient::expire(const std::string& key, int seconds) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "EXPIRE %b %d", key.data(), key.size(), seconds);
    bool ok = (reply && reply->integer == 1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

bool RedisClient::exists(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "EXISTS %b", key.data(), key.size());
    bool ok = (reply && reply->integer > 0);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

long long RedisClient::incr(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "INCR %b", key.data(), key.size());
    long long val = (reply && reply->type == REDIS_REPLY_INTEGER) ? reply->integer : -1;
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return val;
}

long long RedisClient::incrWithExpire(const std::string& key, int seconds) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;

    // Lua 脚本：原子执行 INCR，首次递增时设置 EXPIRE
    static const char* lua =
        "local v = redis.call('INCR', KEYS[1]) "
        "if v == 1 then redis.call('EXPIRE', KEYS[1], ARGV[1]) end "
        "return v";

    redisReply* reply = (redisReply*)redisCommand(ctx,
        "EVAL %s 1 %b %d",
        lua, key.data(), key.size(), seconds);

    long long val = -1;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        val = reply->integer;
    }
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return val;
}

bool RedisClient::setNxEx(const std::string& key, const std::string& value, int seconds) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SET %b %b NX EX %d", key.data(), key.size(),
                                                  value.data(), value.size(), seconds);
    bool acquired = false;
    if (reply) {
        if (reply->type == REDIS_REPLY_STATUS && reply->str && std::strncmp(reply->str, "OK", 2) == 0) {
            acquired = true;
        } else if (reply->type == REDIS_REPLY_NIL) {
            acquired = false;
        }
        freeReplyObject(reply);
    }
    releaseContext(ctx);
    return acquired;
}

long long RedisClient::sadd(const std::string& key, const std::string& member) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SADD %b %b", key.data(), key.size(), member.data(), member.size());
    long long ret = (reply ? reply->integer : -1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

long long RedisClient::srem(const std::string& key, const std::string& member) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SREM %b %b", key.data(), key.size(), member.data(), member.size());
    long long ret = (reply ? reply->integer : -1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

bool RedisClient::sismember(const std::string& key, const std::string& member) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SISMEMBER %b %b", key.data(), key.size(), member.data(), member.size());
    bool ok = (reply && reply->integer == 1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

std::vector<std::string> RedisClient::smembers(const std::string& key) {
    redisContext* ctx = getContext();
    std::vector<std::string> result;
    if (!ctx) return result;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SMEMBERS %b", key.data(), key.size());
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            result.emplace_back(reply->element[i]->str, reply->element[i]->len);
        }
    }
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return result;
}

// 批量删除多个 key（pipeline，一次网络往返）
long long RedisClient::delBatch(const std::vector<std::string>& keys) {
    if (keys.empty()) return 0;
    redisContext* ctx = getContext();
    if (!ctx) return 0;

    // pipeline 批量发送 DEL 命令
    for (const auto& key : keys) {
        redisAppendCommand(ctx, "DEL %b", key.data(), key.size());
    }

    long long totalDeleted = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        redisReply* reply = nullptr;
        if (redisGetReply(ctx, (void**)&reply) == REDIS_OK && reply) {
            if (reply->type == REDIS_REPLY_INTEGER) {
                totalDeleted += reply->integer;
            }
            freeReplyObject(reply);
        }
    }

    releaseContext(ctx);
    return totalDeleted;
}

// 批量设置 Hash 字段并指定 TTL
bool RedisClient::hsetex(const std::string& key, const std::unordered_map<std::string, std::string>& fields, int seconds) {
    if (fields.empty()) return true;
    redisContext* ctx = getContext();
    if (!ctx) return false;

    // pipeline: HSET + EXPIRE
    for (const auto& [field, value] : fields) {
        redisAppendCommand(ctx, "HSET %b %b %b",
                           key.data(), key.size(),
                           field.data(), field.size(),
                           value.data(), value.size());
    }
    redisAppendCommand(ctx, "EXPIRE %b %d", key.data(), key.size(), seconds);

    bool ok = true;
    size_t totalReplies = fields.size() + 1;
    for (size_t i = 0; i < totalReplies; ++i) {
        redisReply* reply = nullptr;
        if (redisGetReply(ctx, (void**)&reply) == REDIS_OK && reply) {
            if (reply->type == REDIS_REPLY_ERROR) ok = false;
            freeReplyObject(reply);
        } else {
            ok = false;
        }
    }

    releaseContext(ctx);
    return ok;
}

long long RedisClient::scard(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "SCARD %b", key.data(), key.size());
    long long ret = (reply ? reply->integer : -1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

long long RedisClient::rpush(const std::string& key, const std::string& value) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "RPUSH %b %b", key.data(), key.size(), value.data(), value.size());
    long long ret = (reply ? reply->integer : -1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

std::vector<std::string> RedisClient::lrange(const std::string& key, int start, int stop) {
    redisContext* ctx = getContext();
    std::vector<std::string> result;
    if (!ctx) return result;
    redisReply* reply = (redisReply*)redisCommand(ctx, "LRANGE %b %d %d", key.data(), key.size(), start, stop);
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
    redisReply* reply = (redisReply*)redisCommand(ctx, "LLEN %b", key.data(), key.size());
    long long ret = (reply ? reply->integer : -1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

bool RedisClient::ltrim(const std::string& key, int start, int stop) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "LTRIM %b %d %d", key.data(), key.size(), start, stop);
    bool ok = (reply && reply->type != REDIS_REPLY_ERROR);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

// ==================== 哈希操作 ====================
bool RedisClient::hset(const std::string& key, const std::string& field, const std::string& value) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HSET %b %b %b", key.data(), key.size(), field.data(), field.size(), value.data(), value.size());
    bool ok = (reply && reply->type != REDIS_REPLY_ERROR);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

std::string RedisClient::hget(const std::string& key, const std::string& field) {
    redisContext* ctx = getContext();
    if (!ctx) return "";
    redisReply* reply = (redisReply*)redisCommand(ctx, "HGET %b %b", key.data(), key.size(), field.data(), field.size());
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
    redisReply* reply = (redisReply*)redisCommand(ctx, "HDEL %b %b", key.data(), key.size(), field.data(), field.size());
    bool ok = (reply && reply->integer > 0);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

bool RedisClient::hexists(const std::string& key, const std::string& field) {
    redisContext* ctx = getContext();
    if (!ctx) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HEXISTS %b %b", key.data(), key.size(), field.data(), field.size());
    bool ok = (reply && reply->integer == 1);
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ok;
}

long long RedisClient::hlen(const std::string& key) {
    redisContext* ctx = getContext();
    if (!ctx) return -1;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HLEN %b", key.data(), key.size());
    long long ret = (reply && reply->type == REDIS_REPLY_INTEGER) ? reply->integer : -1;
    if (reply) freeReplyObject(reply);
    releaseContext(ctx);
    return ret;
}

std::unordered_map<std::string, std::string> RedisClient::hgetall(const std::string& key) {
    redisContext* ctx = getContext();
    std::unordered_map<std::string, std::string> result;
    if (!ctx) return result;
    redisReply* reply = (redisReply*)redisCommand(ctx, "HGETALL %b", key.data(), key.size());
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
    redisReply* reply = (redisReply*)redisCommand(ctx, "HKEYS %b", key.data(), key.size());
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
    redisReply* reply = (redisReply*)redisCommand(ctx, "HVALS %b", key.data(), key.size());
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

    // 使用 pipeline 批量发送命令（%b 二进制安全格式）
    for (const auto& key : keys) {
        redisAppendCommand(ctx, "HGET %b %b", key.data(), key.size(), field.data(), field.size());
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