/*
 * main.cc - AeroImageHost 主程序入口
 *
 * 职责：初始化所有服务组件，启动 HTTP 服务器，等待退出信号。
 *
 * 启动顺序（严格按依赖关系）：
 *   1. Config::load()        → 加载 config.json 配置文件
 *   2. AsyncLog::init()      → 初始化异步日志系统
 *   3. ConnectionPool::init() → 初始化 MySQL 连接池（默认 32 连接）
 *   4. RedisClient::init()   → 初始化 Redis 连接池（默认 16 连接）
 *   5. MinIOClient::init()   → 初始化 MinIO 客户端
 *   6. AeroQueue::start(4)   → 启动异步任务队列（4 个工作线程）
 *   7. HttpServer::start()   → 启动 HTTP 服务器（监听端口）
 *   8. cleanupOrphanChunks() → 清理上一次运行遗留的孤儿分片
 *
 * 后台任务：
 *   - 每小时执行一次 cleanupOrphanChunks()，清理超时的分片上传
 *
 * 退出流程：
 *   收到 SIGINT/SIGTERM → HttpServer::stop() → 等待5秒 → AeroQueue::stop() → ConnectionPool::close() → AsyncLog::stop()
 */
#include "Config.hpp"
#include "Log.hpp"
#include "HttpServer.hpp"
#include "ConnectionPool.hpp"
#include "RedisClient.hpp"
#include "MinIOClient.hpp"
#include "FileMeta.hpp"
#include "AeroQueue.hpp"
#include "Handlers.hpp"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <thread>

std::atomic<bool> running(true);

void signalHandler(int) {
    running = false;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // 加载配置文件
    std::string configFile = (argc > 1) ? argv[1] : "config.json";
    if (!Config::instance().load(configFile)) {
        std::cerr << "Failed to load config file: " << configFile << std::endl;
        return 1;
    }

    // 初始化日志系统
    std::string logFile = Config::instance().getString("log.file", "/opt/image_host/logs/server.log");
    int flushInterval = Config::instance().getInt("log.flush_interval", 3);
    AsyncLog::instance().init(logFile, flushInterval);
    LOG_INFO("日志系统初始化完成，日志文件: " + logFile);

    // 初始化 MySQL 连接池
    auto& pool = ConnectionPool::getInstance();
    if (!pool.init(
            Config::instance().getString("mysql.host"),
            Config::instance().getString("mysql.user"),
            Config::instance().getString("mysql.password"),
            Config::instance().getString("mysql.db"),
            Config::instance().getInt("mysql.port", 3306),
            Config::instance().getInt("mysql.pool_size", 32))) {
        LOG_ERROR("MySQL 连接池初始化失败");
        return 1;
    }
    LOG_INFO("MySQL 连接池初始化成功");

    // 初始化 Redis 连接池
    if (!RedisClient::instance().init(
            Config::instance().getString("redis.host", "127.0.0.1"),
            Config::instance().getInt("redis.port", 6379),
            Config::instance().getInt("redis.pool_size", 16))) {
        LOG_ERROR("Redis 连接池初始化失败");
        return 1;
    }
    LOG_INFO("Redis 连接池初始化成功");

    // 初始化 MinIO 客户端
    if (!MinIOClient::instance().init(
            Config::instance().getString("minio.endpoint"),
            Config::instance().getString("minio.access_key"),
            Config::instance().getString("minio.secret_key"),
            Config::instance().getString("minio.bucket"),
            Config::instance().getString("minio.presign_endpoint", ""))) {
        LOG_ERROR("MinIO 客户端初始化失败");
        return 1;
    }
    LOG_INFO("MinIO 客户端初始化成功");

    // 初始化 AeroQueue（异步任务队列）
    AeroQueue::instance().start(4);
    LOG_INFO("AeroQueue 启动成功");

    // 启动 HTTP 服务
    int httpPort = Config::instance().getInt("http_port", 8082);
    HttpServer server(httpPort);
    server.start();
    LOG_INFO("HTTP 服务启动成功，监听端口: " + std::to_string(httpPort));

    // 启动时清理上一次运行遗留的孤儿分片
    cleanupOrphanChunks();

    // 后台线程：每小时执行一次孤儿分片清理，防止 MinIO 存储空间泄漏
    std::thread cleanupThread([]() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::hours(1));
            if (running) {
                try {
                    cleanupOrphanChunks();
                } catch (const std::exception& e) {
                    LOG_ERROR("[Cleanup] 定时清理失败: " + std::string(e.what()));
                }
            }
        }
    });
    cleanupThread.detach();

    // 主循环等待退出信号
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    LOG_INFO("正在关闭服务...");
    server.stop();
    std::this_thread::sleep_for(std::chrono::seconds(5));
    AeroQueue::instance().stop();
    ConnectionPool::getInstance().close();
    AsyncLog::instance().stop();
    LOG_INFO("服务已安全退出");
    return 0;
}
