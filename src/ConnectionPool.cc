/*
 * ConnectionPool.cc - MySQL 连接池实现
 *
 * 在项目中的作用：管理一组 MySQL 连接，提供获取与归还的接口，避免频繁创建/销毁连接。
 * 核心机制：同步阻塞连接池，使用条件变量等待可用连接，懒验证（30秒间隔 mysql_ping）。
 * 使用方式：ConnectionPool::instance().getConnection() 获取连接，用完调用 releaseConnection() 归还。
 * 推荐：使用 DbGuard RAII 守卫自动管理连接生命周期，防止忘记归还导致连接泄漏。
 */
#include "ConnectionPool.hpp"
#include "Log.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <chrono>

// 连接验证间隔（秒），避免每次获取连接都 mysql_ping
static const int CONNECTION_VALIDATION_INTERVAL = 30;

ConnectionPool& ConnectionPool::getInstance() {
    static ConnectionPool instance;
    return instance;
}

bool ConnectionPool::init(const std::string& host, const std::string& user,
                          const std::string& passwd, const std::string& db,
                          unsigned int port, int poolSize) {
    if (poolSize <= 0) {
        LOG_ERROR("[ConnectionPool] 连接池大小必须大于0");
        return false;
    }
  
    host_ = host;
    user_ = user;
    passwd_ = passwd;
    db_ = db;
    port_ = port;
    poolSize_ = poolSize;
    stopped_ = false;

    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    for (int i = 0; i < poolSize_; ++i) {
        MYSQL* conn = createConnection();
        if (conn) {
            connections_.push({conn, now});
        } else {
            LOG_ERROR("[ConnectionPool] 创建第" + std::to_string(i+1) + "个连接失败");
            while (!connections_.empty()) {
				//出错的时候清理已经成功创建的连接,避免资源泄露
                auto [c, t] = connections_.front();
                mysql_close(c);
                connections_.pop();
            }
            return false;
        }
    }

    LOG_INFO("[ConnectionPool] 连接池初始化完成，创建" + std::to_string(connections_.size()) + "个连接");
    return true;
}

MYSQL* ConnectionPool::createConnection() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        LOG_ERROR("[ConnectionPool] mysql_init失败");
        return nullptr;
    }

    unsigned int timeout = 5;
    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(), passwd_.c_str(),
                            db_.c_str(), port_, nullptr, 0)) {
                            //调用mysql_real_connect建立TCP连接并登录,成功返回conn
        LOG_ERROR("[ConnectionPool] mysql_real_connect失败: " + std::string(mysql_error(conn)));
        mysql_close(conn);
        return nullptr;
    }

    return conn;
}

//这个函数保证了用户拿到的连接始终可用！！！
//优化：只在距离上次验证超过30秒时才真正 ping，减少无效网络往返
MYSQL* ConnectionPool::ensureValidConnection(MYSQL* conn, std::chrono::steady_clock::time_point lastCheck) {
    if (!conn) {
        return createConnection();
    }
    auto elapsed = std::chrono::steady_clock::now() - lastCheck;
    if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < CONNECTION_VALIDATION_INTERVAL) {
        return conn; // 30秒内已验证过，直接信任
    }
    if (mysql_ping(conn) != 0) {
        LOG_WARN("[ConnectionPool] 连接失效，重建连接: " + std::string(mysql_error(conn)));
        mysql_close(conn);
        return createConnection();
    }
    return conn;
}
//顾名思义就是从连接池中取出一个可用连接,如果当前没有空闲连接,调用线程会阻塞等待,知道有连接归还
//或者超时
MYSQL* ConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    waitingCount_.fetch_add(1, std::memory_order_relaxed);

    if (!cv_.wait_for(lock, std::chrono::seconds(5), [this]() { return !connections_.empty() || stopped_; })) {
        waitingCount_.fetch_sub(1, std::memory_order_relaxed);
        LOG_ERROR("[ConnectionPool] 获取连接超时");
        return nullptr;
    }

    waitingCount_.fetch_sub(1, std::memory_order_relaxed);

    if (stopped_) {
        LOG_ERROR("[ConnectionPool] 连接池已停止");
        return nullptr;
    }

    auto [conn, lastCheck] = connections_.front();
    connections_.pop();
    conn = ensureValidConnection(conn, lastCheck);

    int retry = 3;
    while (!conn && retry-- > 0) {
        if (connections_.empty()) {
            // 等待连接归还，同时检查连接池是否已关闭
            if (!cv_.wait_for(lock, std::chrono::seconds(2), [this]() { return !connections_.empty() || stopped_; })) {
                // 等待超时，尝试新建一个连接（池中可能全部被占用）
                lock.unlock();
                MYSQL* newConn = createConnection();
                lock.lock();
                if (newConn) {
                    conn = ensureValidConnection(newConn, std::chrono::steady_clock::now());
                }
                break;
            }
        }
        if (!connections_.empty()) {
            auto [c, t] = connections_.front();
            connections_.pop();
            conn = ensureValidConnection(c, t);
        }
    }

    if (!conn) {
        LOG_ERROR("[ConnectionPool] 重试3次仍无法获取有效连接");
    }

    return conn;
}

void ConnectionPool::releaseConnection(MYSQL* conn) {
    if (!conn || stopped_) {
        if (conn) mysql_close(conn);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    // 归还时直接放入队列，不验证（验证延迟到 getConnection 时按需执行）
    connections_.push({conn, now});
    cv_.notify_one();
}

ConnectionPool::Stats ConnectionPool::getStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s;
    s.idle = static_cast<int>(connections_.size());
    s.active = poolSize_ - s.idle;
    s.waiting = waitingCount_.load(std::memory_order_relaxed);
    return s;
}

void ConnectionPool::close() {  //关闭所有的连接
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    cv_.notify_all();

    while (!connections_.empty()) {
        auto [conn, t] = connections_.front();
        connections_.pop();
        mysql_close(conn);
    }

    LOG_INFO("[ConnectionPool] 连接池已关闭");
}