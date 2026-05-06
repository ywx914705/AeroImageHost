/*
 * EmailVerificationDAO.hpp - 邮箱验证码数据访问层头文件
 *
 * 职责：封装 email_verifications 表的所有 SQL 操作。
 *       通过 ConnectionPool 获取 MySQL 连接，使用 prepared statement 防止 SQL 注入。
 *
 * 在项目中的作用：
 *   - 用户请求发送验证码 → save() 存储（10 分钟有效期）
 *   - 用户提交注册 → verifyCode() 验证有效性
 *   - 注册成功 → markCodeAsUsed() 标记已使用
 *   - 每次操作前自动清理过期验证码
 */
#pragma once

#include <string>

class EmailVerificationDAO {
public:
    static EmailVerificationDAO& instance(); // 获取单例实例

    // 保存验证码到数据库（INSERT，10 分钟有效期）
    bool save(const std::string& email, const std::string& code);
    // 验证验证码是否有效（未过期且未使用）
    bool verifyCode(const std::string& email, const std::string& code);
    // 检查验证码是否已被使用
    bool isCodeUsed(const std::string& email, const std::string& code);
    // 标记验证码为已使用（注册成功后调用）
    void markCodeAsUsed(const std::string& email, const std::string& code);
    // 清理过期的验证码记录（DELETE WHERE expires_at <= NOW()）
    void cleanupExpired();

private:
    EmailVerificationDAO() = default;
    ~EmailVerificationDAO() = default;
    EmailVerificationDAO(const EmailVerificationDAO&) = delete;
    EmailVerificationDAO& operator=(const EmailVerificationDAO&) = delete;
};