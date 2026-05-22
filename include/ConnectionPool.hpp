/*
 * ConnectionPool.hpp - MySQL 连接池头文件
 *
 * 职责：管理一组 MySQL 连接，提供获取和归还接口，避免每次数据库操作都创建新连接。
 *
 * 在项目中的作用：
 *   - 初始化时创建指定数量的 MySQL 连接（默认 32 个）
 *   - getConnection() 获取可用连接（带 5 秒超时等待）
 *   - releaseConnection() 归还连接（归还时不验证，延迟到下次获取时按需验证）
 *   - 30 秒定时验证：超过 30 秒未验证的连接会执行 mysql_ping 检查
 *   - 连接失效时自动重建
 *
 * 设计：单例模式，使用 RAII 确保连接不泄漏。
 */
#pragma once
#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <atomic>

class ConnectionPool {
public:
    static ConnectionPool& getInstance(); // 获取单例实例

    // 初始化连接池：创建指定数量的 MySQL 连接
    bool init(const std::string& host, const std::string& user,
              const std::string& passwd, const std::string& db,
              unsigned int port = 3306, int poolSize = 64);

    MYSQL* getConnection();                    // 获取一个可用连接（带超时等待）
    void releaseConnection(MYSQL* conn);       // 归还连接到连接池
    void close();                              // 关闭所有连接

    // 获取连接池统计信息：活跃连接数、空闲连接数、等待连接数
    struct Stats {
        int active = 0;   // 活跃连接数 = 总大小 - 空闲数
        int idle = 0;     // 空闲连接数
        int waiting = 0;  // 等待连接的线程数
    };
    Stats getStats();

    ConnectionPool(const ConnectionPool&) = delete;            // 禁止拷贝
    ConnectionPool& operator=(const ConnectionPool&) = delete;

private:
    ConnectionPool() = default;
    ~ConnectionPool() { close(); }

    MYSQL* createConnection();                                                  // 创建新的 MySQL 连接
    MYSQL* ensureValidConnection(MYSQL* conn, std::chrono::steady_clock::time_point lastCheck); // 验证并修复连接

    std::string host_;    // 数据库主机地址
    std::string user_;    // 数据库用户名
    std::string passwd_;  // 数据库密码
    std::string db_;      // 数据库名
    unsigned int port_;   // 数据库端口（默认 3306）
    int poolSize_ = 0;    // 连接池大小
    int maxPoolSize_ = 0; // 最大连接池大小（默认 2x poolSize_）

    // 空闲连接队列，每个连接记录上次验证时间
    std::queue<std::pair<MYSQL*, std::chrono::steady_clock::time_point>> connections_;
    std::mutex mutex_;                   // 保护连接队列的互斥锁
    std::condition_variable cv_;         // 条件变量：连接可用时通知等待线程
    bool stopped_ = false;              // 连接池是否已关闭
    std::atomic<int> waitingCount_{0};  // 等待连接的线程数
};