/*
 * Log.cc - 异步日志系统实现
 *
 * 在项目中的作用：提供高性能的异步文件日志，不阻塞业务线程。
 * 核心机制：双缓冲 + 后台线程，前端写入当前缓冲区，后台定时（3秒）将缓冲区内容批量写入文件。
 * 日志格式：JSON 结构化（{"timestamp":"...","level":"...","message":"..."}），便于 ELK 等工具分析。
 * 使用方式：LOG_INFO("message"), LOG_ERROR("message"), LOG_WARN("message") 宏。
 */
#include "Log.hpp"
#include <chrono>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <iomanip>

AsyncLog::AsyncLog()
    : running_(false), flushInterval_(3), buffer_(BUFFER_SIZE), nextBuffer_(BUFFER_SIZE),
      maxFileSize_(50 * 1024 * 1024), currentFileSize_(0), fileIndex_(0) {
    buffer_.clear();   // size = 0, capacity = BUFFER_SIZE
    nextBuffer_.clear();
}

void AsyncLog::init(const std::string& filename, int flushInterval, int maxFileSizeMB) {
    filename_ = filename;
    flushInterval_ = flushInterval;
    maxFileSize_ = static_cast<size_t>(maxFileSizeMB) * 1024 * 1024;
    currentFileSize_ = 0;
    fileIndex_ = 0;
    file_.open(filename, std::ios::app | std::ios::out);
    if (!file_.is_open()) {
        throw std::runtime_error("Log file open failed: " + filename);
    }
    // 获取当前文件大小，用于判断是否需要轮转
    file_.seekp(0, std::ios::end);
    currentFileSize_ = static_cast<size_t>(file_.tellp());
    running_ = true;
    thread_ = std::thread(&AsyncLog::threadFunc, this);
}

void AsyncLog::write(LogLevel level, const std::string& msg) {
    // JSON 结构化日志格式
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::string levelStr;
    switch (level) {
        case LogLevel::DEBUG: levelStr = "DEBUG"; break;
        case LogLevel::INFO:  levelStr = "INFO";  break;
        case LogLevel::WARN:  levelStr = "WARN";  break;
        case LogLevel::ERROR: levelStr = "ERROR"; break;
    }

    // 格式化时间：2026-05-06T10:30:00.123Z（使用线程安全的 localtime_r）
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();

    // 完整 JSON 字符串转义（RFC 8259）
    std::string escaped_msg;
    escaped_msg.reserve(msg.size() + msg.size() / 4);
    for (unsigned char c : msg) {
        switch (c) {
            case '"':  escaped_msg += "\\\""; break;
            case '\\': escaped_msg += "\\\\"; break;
            case '\n': escaped_msg += "\\n";  break;
            case '\r': escaped_msg += "\\r";  break;
            case '\t': escaped_msg += "\\t";  break;
            case '\b': escaped_msg += "\\b";  break;
            case '\f': escaped_msg += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    escaped_msg += buf;
                } else {
                    escaped_msg += static_cast<char>(c);
                }
        }
    }

    std::string logLine = "{\"timestamp\":\"" + ss.str() + "\",\"level\":\"" + levelStr +
                          "\",\"message\":\"" + escaped_msg + "\"}\n";

    std::unique_lock<std::mutex> lock(mutex_);
    // 如果当前缓冲区剩余空间不足，先交换
    if (buffer_.size() + logLine.size() > BUFFER_SIZE) {
        // 将当前缓冲区移到待写队列
        buffers_.push_back(std::move(buffer_));
        // 使用备用缓冲区作为新的当前缓冲区
        if (!nextBuffer_.empty()) {
            buffer_ = std::move(nextBuffer_);
        } else {
            buffer_.resize(BUFFER_SIZE);
            buffer_.clear();
        }
        // 通知后端线程有数据可写
        cond_.notify_one();
    }
    // 将日志行追加到当前缓冲区
    buffer_.insert(buffer_.end(), logLine.begin(), logLine.end());
}

void AsyncLog::stop() {
    running_ = false;
    cond_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
    // 刷盘所有剩余数据
    std::unique_lock<std::mutex> lock(mutex_);
    if (!buffer_.empty()) {
        file_.write(buffer_.data(), buffer_.size());
        buffer_.clear();
    }
    for (const auto& buf : buffers_) {
        if (!buf.empty()) {
            file_.write(buf.data(), buf.size());
        }
    }
    file_.flush();
    file_.close();
}

void AsyncLog::threadFunc() {
    while (running_) {
        std::vector<std::vector<char>> buffersToWrite;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // 等待条件：有待写缓冲区 或 定时超时
            cond_.wait_for(lock, std::chrono::seconds(flushInterval_), [this]() {
                return !buffers_.empty() || !running_;
            });
            // 将当前缓冲区也加入待写队列（如果有内容）
            if (!buffer_.empty()) {
                buffers_.push_back(std::move(buffer_));
                if (!nextBuffer_.empty()) {
                    buffer_ = std::move(nextBuffer_);
                } else {
                    buffer_.resize(BUFFER_SIZE);
                    buffer_.clear();
                }
            }
            // 取出所有待写缓冲区
            buffersToWrite.swap(buffers_);
        }

        // 写入文件（无锁），超限时执行轮转
        for (const auto& buf : buffersToWrite) {
            if (!buf.empty()) {
                if (currentFileSize_ + buf.size() > maxFileSize_) {
                    rotateLog();
                }
                file_.write(buf.data(), buf.size());
                currentFileSize_ += buf.size();
            }
        }
        file_.flush();

        // 回收缓冲区，用于备用
        {
            std::unique_lock<std::mutex> lock(mutex_);
            for (auto& buf : buffersToWrite) {
                buf.clear();
                if (nextBuffer_.empty()) {
                    nextBuffer_ = std::move(buf);
                }
                // 否则丢弃（内存自动释放）
            }
        }
    }
}

void AsyncLog::rotateLog() {
    file_.flush();
    file_.close();
    fileIndex_++;
    // 生成轮转文件名：app.log.1, app.log.2, ...
    std::string rotatedName = filename_ + "." + std::to_string(fileIndex_);
    std::rename(filename_.c_str(), rotatedName.c_str());
    file_.open(filename_, std::ios::app | std::ios::out);
    currentFileSize_ = 0;
}