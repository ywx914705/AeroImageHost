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
#include <condition_variable>

class RedisClient {
public:
    static RedisClient& instance(); // 获取单例实例

    // 初始化连接池：创建指定数量的 Redis 连接
    bool init(const std::string& host, int port, int poolSize = 8);
    // 获取一个可用连接（带 3 秒超时等待 + 健康检查 + 自动重建）
    redisContext* getContext();
    // 归还连接到连接池
    void releaseContext(redisContext* ctx);

    // ========== String 操作 ==========
    bool set(const std::string& key, const std::string& value);   // 设置键值对
    std::string get(const std::string& key);                       // 获取值
    bool del(const std::string& key);                              // 删除键
    bool expire(const std::string& key, int seconds);              // 设置过期时间
    bool exists(const std::string& key);                           // 检查键是否存在
    long long incr(const std::string& key);                        // 原子递增，返回递增后的值

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

private:
    RedisClient() = default;
    std::queue<redisContext*> pool_;      // 空闲连接队列
    std::mutex mutex_;                     // 保护连接队列的互斥锁
    std::condition_variable cv_;           // 连接可用时的通知机制
    std::string host_;                     // Redis 服务器地址
    int port_;                             // Redis 端口
    int poolSize_;                         // 连接池大小
};