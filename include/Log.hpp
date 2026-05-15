/*
 * Log.hpp - 异步日志系统头文件
 *
 * 在项目中的作用：声明 AsyncLog 单例类，提供异步文件日志能力。
 * 使用宏：LOG_INFO(msg), LOG_WARN(msg), LOG_ERROR(msg) — 自动添加时间戳和日志级别。
 */
#pragma once
#include <atomic>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>

// 日志级别
enum class LogLevel { DEBUG, INFO, WARN, ERROR };

// 高性能异步日志（双缓冲区 + 条件变量）
class AsyncLog {
public:
    static AsyncLog &instance() {
        static AsyncLog inst;
        return inst;
    }

    // 初始化日志文件，flushInterval秒强制刷盘，maxFileSizeMB 为单文件最大MB数
    void init(const std::string &filename, int flushInterval = 3, int maxFileSizeMB = 50);
    // 写入日志（前端）
    void write(LogLevel level, const std::string &msg);
    // 停止日志线程
    void stop();

private:
    AsyncLog();
    ~AsyncLog() { stop(); }

    // 日志线程入口（后端刷盘）
    void threadFunc();

    std::atomic<bool> running_;
    int flushInterval_;                     // 强制刷盘间隔（秒）
    std::string filename_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;           // 通知后端线程
    std::vector<char> buffer_;               // 当前写入缓冲区
    std::vector<char> nextBuffer_;           // 备用缓冲区（用于快速交换）
    std::vector<std::vector<char>> buffers_; // 待写入文件的缓冲区队列
    std::ofstream file_;
    size_t maxFileSize_;                // 单个日志文件最大字节数
    size_t currentFileSize_;            // 当前日志文件已写字节数
    int fileIndex_;                     // 轮转文件编号

    // 执行日志文件轮转：关闭当前文件，打开新文件
    void rotateLog();

    static const size_t BUFFER_SIZE = 4 * 1024 * 1024; // 4MB
};

// 日志宏（使用 AERO_ 前缀避免与 Drogon 日志宏冲突）
#define AERO_LOG_DEBUG(msg) AsyncLog::instance().write(LogLevel::DEBUG, msg)
#define AERO_LOG_INFO(msg) AsyncLog::instance().write(LogLevel::INFO, msg)
#define AERO_LOG_WARN(msg) AsyncLog::instance().write(LogLevel::WARN, msg)
#define AERO_LOG_ERROR(msg) AsyncLog::instance().write(LogLevel::ERROR, msg)

// 兼容旧宏名
#ifndef LOG_DEBUG
#define LOG_DEBUG(msg) AERO_LOG_DEBUG(msg)
#endif
#ifndef LOG_INFO
#define LOG_INFO(msg) AERO_LOG_INFO(msg)
#endif
#ifndef LOG_WARN
#define LOG_WARN(msg) AERO_LOG_WARN(msg)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(msg) AERO_LOG_ERROR(msg)
#endif