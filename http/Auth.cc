#include "Auth.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include "RedisClient.hpp"
#include <random>
#include <sstream>
#include <cpprest/http_headers.h>

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

std::shared_ptr<UserInfo> Auth::verify(const web::http::http_request& req) {
    auto auth_hdr = req.headers().find(web::http::header_names::authorization);
    if (auth_hdr == req.headers().end()) {
        LOG_WARN("[Auth] No Authorization header found");
        return nullptr;
    }

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

    LOG_INFO("[Auth] Verifying token: " + token.substr(0, 16) + "...");
    std::string key = "auth_token:" + token;
    RedisClient& redis = RedisClient::instance();
    std::string userIdStr = redis.get(key);

    LOG_INFO("[Auth] Redis get result for '" + key.substr(0, 20) + "...': '" + userIdStr + "'");

    // 清理 userIdStr 中的空白字符
    userIdStr.erase(std::remove_if(userIdStr.begin(), userIdStr.end(), ::isspace), userIdStr.end());
    if (userIdStr.empty()) {
        LOG_WARN("[Auth] Token not found in Redis or invalid: " + token.substr(0, 16) + "...");
        return nullptr;
    }

    try {
        int user_id = std::stoi(userIdStr);
        LOG_INFO("[Auth] Token verified successfully, user_id: " + std::to_string(user_id));
        auto user = std::make_shared<UserInfo>();
        user->user_id = user_id;
        return user;
    } catch (const std::exception& e) {
        LOG_ERROR("[Auth] Failed to parse user ID from Redis: " + std::string(e.what()));
        return nullptr;
    }
}

std::string Auth::generateToken(int user_id, const std::string& /* username */) {
    std::string token = generateRandomString(32);
    std::string key = "auth_token:" + token;
    RedisClient& redis = RedisClient::instance();

    // 验证 Redis set 和 expire 是否成功
    if (!redis.set(key, std::to_string(user_id))) {
        LOG_ERROR("Failed to store auth token in Redis");
        return ""; // 返回空字符串表示失败
    }

    if (!redis.expire(key, 86400)) { // 24 小时
        LOG_ERROR("Failed to set token expiration in Redis");
        redis.del(key); // 清理已设置的 key
        return "";
    }

    return token;
}

void Auth::revokeToken(const std::string& token) {
    std::string key = "auth_token:" + token;
    RedisClient::instance().del(key);
}
