#pragma once

#include "Config.hpp"
#include "Log.hpp"
#include <string>
#include <vector>

class ConfigValidator {
public:
    static bool validate() {
        auto& cfg = Config::instance();
        std::vector<std::string> errors;

        // MySQL 必需配置
        if (cfg.getString("mysql.host").empty())
            errors.push_back("mysql.host is required");
        if (cfg.getString("mysql.user").empty())
            errors.push_back("mysql.user is required");
        if (cfg.getString("mysql.db").empty())
            errors.push_back("mysql.db is required");
        int mysqlPort = cfg.getInt("mysql.port", 3306);
        if (mysqlPort <= 0 || mysqlPort > 65535)
            errors.push_back("mysql.port must be 1-65535");
        int mysqlPool = cfg.getInt("mysql.pool_size", 64);
        if (mysqlPool <= 0 || mysqlPool > 512)
            errors.push_back("mysql.pool_size must be 1-512");

        // Redis 必需配置
        if (cfg.getString("redis.host").empty())
            errors.push_back("redis.host is required");
        int redisPort = cfg.getInt("redis.port", 6379);
        if (redisPort <= 0 || redisPort > 65535)
            errors.push_back("redis.port must be 1-65535");
        int redisPool = cfg.getInt("redis.pool_size", 32);
        if (redisPool <= 0 || redisPool > 256)
            errors.push_back("redis.pool_size must be 1-256");

        // MinIO 必需配置
        if (cfg.getString("minio.endpoint").empty())
            errors.push_back("minio.endpoint is required");
        if (cfg.getString("minio.access_key").empty())
            errors.push_back("minio.access_key is required");
        if (cfg.getString("minio.secret_key").empty())
            errors.push_back("minio.secret_key is required");
        if (cfg.getString("minio.bucket").empty())
            errors.push_back("minio.bucket is required");

        // HTTP 配置校验
        int httpPort = cfg.getInt("http_port", 8082);
        if (httpPort <= 0 || httpPort > 65535)
            errors.push_back("http_port must be 1-65535");
        int maxFileSize = cfg.getInt("max_file_size", 100 * 1024 * 1024);
        if (maxFileSize <= 0 || maxFileSize > 10L * 1024 * 1024 * 1024)
            errors.push_back("max_file_size must be 1-10GB");

        int statsCacheTtl = cfg.getInt("stats_cache_ttl_seconds", 60);
        if (statsCacheTtl < 0 || statsCacheTtl > 86400)
            errors.push_back("stats_cache_ttl_seconds must be 0-86400 (0 disables Redis cache for /api/stats)");

        // 安全配置校验
        int maxLogin = cfg.getInt("security.max_login_attempts", 5);
        if (maxLogin <= 0 || maxLogin > 100)
            errors.push_back("security.max_login_attempts must be 1-100");
        int maxLoginIp = cfg.getInt("security.max_login_attempts_per_ip", 30);
        if (maxLoginIp <= 0 || maxLoginIp > 500)
            errors.push_back("security.max_login_attempts_per_ip must be 1-500");
        int loginWindow = cfg.getInt("security.login_window_seconds", 900);
        if (loginWindow <= 0 || loginWindow > 86400)
            errors.push_back("security.login_window_seconds must be 1-86400");
        int sendCodeMax = cfg.getInt("security.max_send_code_requests_per_ip_per_hour", 10);
        if (sendCodeMax <= 0 || sendCodeMax > 200)
            errors.push_back("security.max_send_code_requests_per_ip_per_hour must be 1-200");
        int sendCodeWindow = cfg.getInt("security.send_code_ip_window_seconds", 3600);
        if (sendCodeWindow <= 0 || sendCodeWindow > 86400)
            errors.push_back("security.send_code_ip_window_seconds must be 1-86400");

        int maxPage = cfg.getInt("files_list.max_page_size", 100);
        if (maxPage < 1 || maxPage > 500)
            errors.push_back("files_list.max_page_size must be 1-500");
        int maxOff = cfg.getInt("files_list.max_offset", 5000000);
        if (maxOff < 0 || maxOff > 50000000)
            errors.push_back("files_list.max_offset must be 0-50000000");
        int maxSearchKw = cfg.getInt("files_list.max_search_keyword_length", 128);
        if (maxSearchKw < 0 || maxSearchKw > 512)
            errors.push_back("files_list.max_search_keyword_length must be 0-512 (0 = no truncation)");

        // 日志配置校验
        if (cfg.getString("log.file").empty())
            errors.push_back("log.file is required");
        int flushInterval = cfg.getInt("log.flush_interval", 3);
        if (flushInterval <= 0 || flushInterval > 60)
            errors.push_back("log.flush_interval must be 1-60");

        // 输出校验结果
        if (!errors.empty()) {
            AsyncLog::instance().write(LogLevel::ERROR, "=== Configuration Validation Failed ===");
            for (const auto& err : errors) {
                AsyncLog::instance().write(LogLevel::ERROR, "  - " + err);
            }
            return false;
        }

        // 输出有效配置摘要（隐藏敏感信息）
        AsyncLog::instance().write(LogLevel::INFO, "Configuration validated successfully:");
        AsyncLog::instance().write(LogLevel::INFO, "  MySQL: " + cfg.getString("mysql.host") + ":" +
                                   std::to_string(mysqlPort) + "/" + cfg.getString("mysql.db") +
                                   " (pool=" + std::to_string(mysqlPool) + ")");
        AsyncLog::instance().write(LogLevel::INFO, "  Redis: " + cfg.getString("redis.host") + ":" +
                                   std::to_string(redisPort) + " (pool=" + std::to_string(redisPool) + ")");
        AsyncLog::instance().write(LogLevel::INFO, "  MinIO: " + cfg.getString("minio.endpoint") +
                                   " bucket=" + cfg.getString("minio.bucket"));
        AsyncLog::instance().write(LogLevel::INFO, "  HTTP: port=" + std::to_string(httpPort) +
                                   " max_file=" + std::to_string(maxFileSize / 1024 / 1024) + "MB");

        if (cfg.getString("security.cors_origin", "*") == "*") {
            AsyncLog::instance().write(LogLevel::WARN,
                "security.cors_origin is \"*\". For production, set an explicit origin (e.g. https://your.domain).");
        }
        return true;
    }
};
