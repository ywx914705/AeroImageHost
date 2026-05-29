/*
 * RedisClient.hpp - Redis 连接池客户端头文件
 *
 * 职责：管理 Redis 连接池，封装常用的 Redis 操作（String/Set/List/Hash）。
 *
 * 在项目中的作用：
 *   - Auth 模块：存储/验证认证 Token（auth_token:<token> → user_id）
 *   - RateLimiter：记录登录失败次数（ratelimit:login:<account> → count）
 *   - 分片上传：跟踪活跃上传（upload_active:<upload_id>、active_multipart_uploads 集合）
 *   - 缩略图缓存：异步写入缩略图时作为任务队列的消费者
 *
 * 设计：单例模式，连接池默认 16 个连接，每次操作获取/归还一个连接。
 */
#pragma once

#include <hiredis/hiredis.h>
#include <string>
#include <queue>
#include <mutex>
#include <memory>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <condition_variable>
#include <array>
#include <atomic>

class RedisClient {
public:
    static RedisClient& instance(); // 获取单例实例

    // 初始化连接池：创建指定数量的 Redis 连接（password 为空则不发 AUTH，兼容无密码 Redis）
    bool init(const std::string& host, int port, int poolSize = 32, const std::string& password = "");
    // 获取一个可用连接（带 3 秒超时等待 + 健康检查 + 自动重建）
    redisContext* getContext(size_t* shardIdx = nullptr);
    // 归还连接到连接池
    void releaseContext(redisContext* ctx, size_t shardIdx = SIZE_MAX);

    // 获取连接池统计信息：活跃连接数、空闲连接数
    struct Stats {
        int active = 0;   // 活跃连接数 = 总大小 - 空闲数
        int idle = 0;     // 空闲连接数
    };
    Stats getStats();

    void close();

    // 执行 PING 命令（用于预热和健康检查）
    std::string ping();
    // 获取 key 的类型（用于缓存兼容性检查）
    std::string type(const std::string& key);

    // ========== String 操作 ==========
    bool set(const std::string& key, const std::string& value);   // 设置键值对
    bool setex(const std::string& key, const std::string& value, int seconds); // 原子设置键值对+过期时间
    std::string get(const std::string& key);                       // 获取值
    bool del(const std::string& key);                              // 删除键
    bool expire(const std::string& key, int seconds);              // 设置过期时间
    bool exists(const std::string& key);                           // 检查键是否存在
    long long incr(const std::string& key);                        // 原子递增，返回递增后的值
    long long incrWithExpire(const std::string& key, int seconds); // 原子递增+设置过期（Lua 脚本）
    // SET key value NX EX ttl — 仅当 key 不存在时写入；用于缩略图生成互斥，避免惊群重复拉原图
    bool setNxEx(const std::string& key, const std::string& value, int seconds);

    // ========== Set 操作 ==========
    long long sadd(const std::string& key, const std::string& member);   // 添加元素到集合
    long long srem(const std::string& key, const std::string& member);   // 从集合移除元素
    bool sismember(const std::string& key, const std::string& member);   // 检查元素是否在集合中
    std::vector<std::string> smembers(const std::string& key);           // 获取集合所有元素
    long long scard(const std::string& key);                             // 获取集合大小

    // ========== List 操作 ==========
    long long rpush(const std::string& key, const std::string& value);   // 从右侧插入元素
    std::vector<std::string> lrange(const std::string& key, int start, int stop); // 获取列表范围
    long long llen(const std::string& key);                              // 获取列表长度
    bool ltrim(const std::string& key, int start, int stop);            // 裁剪列表

    // ========== Hash 操作 ==========
    bool hset(const std::string& key, const std::string& field, const std::string& value); // 设置哈希字段
    std::string hget(const std::string& key, const std::string& field);                     // 获取哈希字段
    bool hdel(const std::string& key, const std::string& field);                           // 删除哈希字段
    bool hexists(const std::string& key, const std::string& field);                        // 检查字段是否存在
    long long hlen(const std::string& key);                                                 // 获取哈希大小
    std::unordered_map<std::string, std::string> hgetall(const std::string& key);           // 获取所有字段
    std::vector<std::string> hkeys(const std::string& key);                                 // 获取所有字段名
    std::vector<std::string> hvals(const std::string& key);                                 // 获取所有字段值

    // ========== 批量操作（pipeline） ==========
    // 批量获取多个 key 的同一字段值（减少网络往返）
    std::vector<std::string> multiHget(const std::vector<std::string>& keys, const std::string& field);
    // 批量删除多个 key（pipeline，一次网络往返）
    long long delBatch(const std::vector<std::string>& keys);
    // 批量设置 Hash 字段并指定 TTL
    bool hsetex(const std::string& key, const std::unordered_map<std::string, std::string>& fields, int seconds);

private:
    RedisClient() = default;

    static constexpr int SHARD_COUNT = 8;

    struct Shard {
        std::queue<std::pair<redisContext*, std::chrono::steady_clock::time_point>> pool;
        std::mutex mutex;
        std::condition_variable cv;
    };

    std::array<Shard, SHARD_COUNT> shards_;
    std::atomic<bool> stopped_{false};

    std::string host_;
    int port_;
    int poolSize_;
    std::string password_;

    size_t getShard() const;
};