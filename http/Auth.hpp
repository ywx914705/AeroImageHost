/*
 * Auth.hpp - Token 认证模块头文件
 *
 * 职责：管理用户认证 Token 的生成、验证和撤销。
 * 原理：登录成功后生成 32 位随机 Token，存入 Redis（24小时过期），
 *       后续请求通过 Authorization: Bearer <token> 头部传递，由 verify() 验证。
 *
 * 在项目中的作用：所有需要认证的 API 端点都调用 Auth::verify() 检查用户身份。
 */
#pragma once
#include <string>
#include <memory>
#include <cpprest/http_msg.h>

// 用户信息结构体，验证成功后填充
struct UserInfo {
    int user_id; // 用户 ID（对应 MySQL users 表的 id 字段）
};

class Auth {
public:
    // 验证 HTTP 请求中的 Bearer Token，成功返回 UserInfo，失败返回 nullptr
    static std::shared_ptr<UserInfo> verify(const web::http::http_request& req);
    // 生成新的认证 Token 并存入 Redis（24小时有效），返回 Token 字符串
    static std::string generateToken(int user_id, const std::string& username);
    // 撤销 Token（从 Redis 中删除）
    static void revokeToken(const std::string& token);
};
