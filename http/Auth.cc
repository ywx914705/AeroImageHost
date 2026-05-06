/*
 * Auth 模块 - Token 认证实现
 *
 * 核心流程：
 *   1. 用户登录 → generateToken() → 生成 32 位随机字符串 → 存入 Redis（auth_token:<token> = user_id，TTL=24h）
 *   2. API 请求 → verify() → 从 Authorization 头提取 token → Redis 查找 user_id → 返回 UserInfo
 *   3. 登出 → revokeToken() → 从 Redis 删除 token
 *
 * Token 存储在 Redis 中，key 格式为 "auth_token:<token>"，value 为 user_id 字符串。
 */
#include "Auth.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include "RedisClient.hpp"
#include <random>
#include <sstream>
#include <cpprest/http_headers.h>

// 生成指定长度的随机字符串（字母+数字），用于 Token 生成
static std::string generateRandomString(size_t length) {
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    return result;
}

// 验证 HTTP 请求中的 Bearer Token
// 从 Authorization 头提取 token → Redis 查找对应 user_id → 返回 UserInfo
std::shared_ptr<UserInfo> Auth::verify(const web::http::http_request& req) {
    // 1. 检查是否存在 Authorization 头
    auto auth_hdr = req.headers().find(web::http::header_names::authorization);
    if (auth_hdr == req.headers().end()) {
        LOG_WARN("[Auth] No Authorization header found");
        return nullptr;
    }

    // 2. 验证格式为 "Bearer <token>"
    const auto& auth = auth_hdr->second;
    if (auth.size() < 7 || (auth.substr(0, 7) != "Bearer " && auth.substr(0, 7) != "bearer ")) {
        LOG_WARN("[Auth] Invalid Authorization format: " + utility::conversions::to_utf8string(auth));
        return nullptr;
    }
    std::string token = auth.substr(7);

    // 移除 token 中可能的空白字符
    token.erase(std::remove_if(token.begin(), token.end(), ::isspace), token.end());
    if (token.empty()) {
        LOG_WARN("[Auth] Token is empty after removing whitespace");
        return nullptr;
    }

    // 3. 从 Redis 查找 token 对应的 user_id
    LOG_INFO("[Auth] 验证 token: " + token.substr(0, 8) + "...");
    std::string key = "auth_token:" + token;
    RedisClient& redis = RedisClient::instance();
    std::string userIdStr = redis.get(key);

    // 清理 userIdStr 中的空白字符
    userIdStr.erase(std::remove_if(userIdStr.begin(), userIdStr.end(), ::isspace), userIdStr.end());
    if (userIdStr.empty()) {
        LOG_WARN("[Auth] Token 无效或已过期");
        return nullptr;
    }

    // 4. 解析 user_id 并返回 UserInfo
    try {
        int user_id = std::stoi(userIdStr);
        auto user = std::make_shared<UserInfo>();
        user->user_id = user_id;
        return user;
    } catch (const std::exception& e) {
        LOG_ERROR("[Auth] Failed to parse user ID from Redis: " + std::string(e.what()));
        return nullptr;
    }
}

// 生成认证 Token：创建 32 位随机字符串，存入 Redis 并设置 24 小时过期
std::string Auth::generateToken(int user_id, const std::string& /* username */) {
    std::string token = generateRandomString(32);
    std::string key = "auth_token:" + token;
    RedisClient& redis = RedisClient::instance();

    // 存储 token → user_id 映射
    if (!redis.set(key, std::to_string(user_id))) {
        LOG_ERROR("Failed to store auth token in Redis");
        return ""; // 返回空字符串表示失败
    }

    // 设置 24 小时过期，过期后 token 自动失效
    if (!redis.expire(key, 86400)) {
        LOG_ERROR("Failed to set token expiration in Redis");
        redis.del(key); // 清理已设置的 key
        return "";
    }

    return token;
}

// 撤销 Token：从 Redis 中删除，立即失效
void Auth::revokeToken(const std::string& token) {
    std::string key = "auth_token:" + token;
    RedisClient::instance().del(key);
}
