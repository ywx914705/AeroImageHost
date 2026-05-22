#include "Config.hpp"
#include "ConfigValidator.hpp"
#include "Log.hpp"
#include "ConnectionPool.hpp"
#include "RedisClient.hpp"
#include "MinIOClient.hpp"
#include "AeroQueue.hpp"
#include "Handlers.hpp"
#include "FileMeta.hpp"
#include "MetricsCollector.hpp"
#include "WatermarkProcessor.hpp"
#include "FileAccessHandler.hpp"
#include "BackgroundService.hpp"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <random>

#include <drogon/drogon.h>

static std::atomic<bool> running(true);
static std::condition_variable shutdownCv;
static std::mutex shutdownMutex;

void signalHandler(int) {
    running = false;
    shutdownCv.notify_all();
    drogon::app().quit();
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string configFile = (argc > 1) ? argv[1] : "config.json";
    if (!Config::instance().load(configFile)) {
        std::cerr << "Failed to load config file: " << configFile << std::endl;
        return 1;
    }

    std::string logFile = Config::instance().getString("log.file", "/opt/image_host/logs/server.log");
    int flushInterval = Config::instance().getInt("log.flush_interval", 3);
    AsyncLog::instance().init(logFile, flushInterval);
    AsyncLog::instance().write(LogLevel::INFO, "Log system initialized: " + logFile);

    if (!ConfigValidator::validate()) {
        std::cerr << "Configuration validation failed. Check log for details." << std::endl;
        return 1;
    }

    auto& pool = ConnectionPool::getInstance();
    if (!pool.init(
            Config::instance().getString("mysql.host"),
            Config::instance().getString("mysql.user"),
            Config::instance().getString("mysql.password"),
            Config::instance().getString("mysql.db"),
            Config::instance().getInt("mysql.port", 3306),
            Config::instance().getInt("mysql.pool_size", 64))) {
        AsyncLog::instance().write(LogLevel::ERROR, "MySQL connection pool init failed");
        return 1;
    }
    AsyncLog::instance().write(LogLevel::INFO, "MySQL connection pool initialized");

    if (!RedisClient::instance().init(
            Config::instance().getString("redis.host", "127.0.0.1"),
            Config::instance().getInt("redis.port", 6379),
            Config::instance().getInt("redis.pool_size", 32),
            Config::instance().getString("redis.password", ""))) {
        AsyncLog::instance().write(LogLevel::ERROR, "Redis connection pool init failed");
        return 1;
    }
    AsyncLog::instance().write(LogLevel::INFO, "Redis connection pool initialized");

    if (!MinIOClient::instance().init(
            Config::instance().getString("minio.endpoint"),
            Config::instance().getString("minio.access_key"),
            Config::instance().getString("minio.secret_key"),
            Config::instance().getString("minio.bucket"),
            Config::instance().getString("minio.presign_endpoint", ""))) {
        AsyncLog::instance().write(LogLevel::ERROR, "MinIO client init failed");
        return 1;
    }
    AsyncLog::instance().write(LogLevel::INFO, "MinIO client initialized");

    int queueThreads = Config::instance().getInt("queue_threads", std::max(4, (int)std::thread::hardware_concurrency()));
    AeroQueue::instance().start(queueThreads);
    AsyncLog::instance().write(LogLevel::INFO, "AeroQueue started");

    if (!WatermarkProcessor::initialize()) {
        AsyncLog::instance().write(LogLevel::WARN, "WatermarkProcessor init failed, watermark feature disabled");
    } else {
        AsyncLog::instance().write(LogLevel::INFO, "WatermarkProcessor initialized");
    }

    try {
        cleanupOrphanChunks();
    } catch (const std::exception& e) {
        AsyncLog::instance().write(LogLevel::WARN, "[Cleanup] Startup cleanup failed: " + std::string(e.what()));
    }

    BackgroundService bgService;
    bgService.start(running);

    {
        AsyncLog::instance().write(LogLevel::INFO, "[Warmup] Starting connection pool warmup...");
        try {
            FileMetaDAO::instance().countByUserWithSearch(0, "");
            AsyncLog::instance().write(LogLevel::INFO, "[Warmup] MySQL pool warmed up");
        } catch (...) {
            AsyncLog::instance().write(LogLevel::WARN, "[Warmup] MySQL pool warmup failed");
        }
        std::string pong = RedisClient::instance().ping();
        if (pong == "PONG") {
            AsyncLog::instance().write(LogLevel::INFO, "[Warmup] Redis pool warmed up");
        } else {
            AsyncLog::instance().write(LogLevel::WARN, "[Warmup] Redis pool warmup failed: " + pong);
        }
    }

    int httpPort = Config::instance().getInt("http_port", 8082);
    int numThreads = Config::instance().getInt("http_threads", std::max(1, (int)std::thread::hardware_concurrency()));
    std::string corsOrigin = Config::instance().getString("security.cors_origin", "*");

    AsyncLog::instance().write(LogLevel::INFO, "HTTP server starting on port " + std::to_string(httpPort) + " with " + std::to_string(numThreads) + " threads");

    auto corsHandler = [corsOrigin](const drogon::HttpRequestPtr &,
                                     std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->addHeader("Access-Control-Allow-Origin", corsOrigin);
        resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        resp->addHeader("Access-Control-Expose-Headers", "*");
        resp->addHeader("Access-Control-Max-Age", "86400");
        callback(resp);
    };

    const std::vector<std::string> apiPaths = {
        "/api/auth/login", "/api/auth/register", "/api/auth/send-code", "/api/auth/email-register",
        "/api/upload", "/api/upload/presign", "/api/upload/confirm",
        "/api/upload/multipart/init", "/api/upload/multipart/chunk",
        "/api/upload/multipart/complete", "/api/upload/multipart/cleanup",
        "/api/files",
        "/api/health", "/api/stats", "/api/metrics",
        "/api/files/batch-delete", "/api/monitor", "/api/shutdown", "/api/cleanup"
    };
    for (const auto& path : apiPaths) {
        drogon::app().registerHandler(path, corsHandler, {drogon::Options});
    }

    drogon::app().registerPreHandlingAdvice(
        [](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&,
           std::function<void()> &&chain) {
            auto attrs = req->getAttributes();
            if (attrs) {
                attrs->insert("metrics_start", std::chrono::steady_clock::now());

                // Generate request ID if not provided by client
                std::string reqId = req->getHeader("X-Request-Id");
                if (reqId.empty()) {
                    static thread_local std::random_device rd;
                    static thread_local std::mt19937 gen(rd());
                    static thread_local std::uniform_int_distribution<> dis(0, 15);
                    reqId.reserve(16);
                    for (int i = 0; i < 16; ++i) {
                        reqId += "0123456789abcdef"[dis(gen)];
                    }
                }
                attrs->insert("request_id", reqId);
            }
            chain();
        });

    drogon::app().registerPostHandlingAdvice(
        [corsOrigin](const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &resp) {
            resp->addHeader("Access-Control-Allow-Origin", corsOrigin);

            auto attrs = req->getAttributes();
            if (attrs && attrs->find("request_id")) {
                std::string reqId = attrs->get<std::string>("request_id");
                resp->addHeader("X-Request-Id", reqId);
            }

            double durationMs = 0;
            if (attrs && attrs->find("metrics_start")) {
                auto startTime = attrs->get<std::chrono::steady_clock::time_point>("metrics_start");
                durationMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - startTime).count();
            }
            std::string path = std::string(req->path());
            int statusCode = static_cast<int>(resp->statusCode());
            MetricsCollector::instance().recordRequest(path, durationMs > 0 ? durationMs : 1.0, statusCode);
        });

    drogon::app().registerPreSendingAdvice(
        [](const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &resp) {
            if (req->method() == drogon::Options) {
                resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
                resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
                resp->addHeader("Access-Control-Expose-Headers", "*");
                resp->addHeader("Access-Control-Max-Age", "86400");
            }
        });

    drogon::app().registerHandler(
        "/api/i/{1}",
        [](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback,
           const std::string &file_id) {
            fileAccessHandler(req, std::move(callback), file_id);
        },
        {drogon::Get});

    drogon::app()
        .addListener("0.0.0.0", httpPort)
        .setThreadNum(numThreads)
        .setClientMaxBodySize(100 * 1024 * 1024)
        .setClientMaxMemoryBodySize(10 * 1024 * 1024)
        .setDocumentRoot("./www")
        .setFileTypes({"html","css","js","json","png","jpg","jpeg","gif","ico","svg","woff","woff2","ttf","map"})
        .run();

    AsyncLog::instance().write(LogLevel::INFO, "Shutting down...");

    bgService.stop();
    AeroQueue::instance().stop();
    ConnectionPool::getInstance().close();
    AsyncLog::instance().write(LogLevel::INFO, "Service safely exited");
    AsyncLog::instance().stop();
    return 0;
}
