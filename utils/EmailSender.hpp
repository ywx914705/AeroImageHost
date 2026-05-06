/*
 * EmailSender.hpp - 邮件发送模块头文件
 *
 * 职责：通过 SMTP 协议发送邮件，当前主要用于发送邮箱验证码。
 *
 * 在项目中的作用：
 *   - 用户请求发送验证码 → EmailSender::sendVerificationEmail() → 异步通过 SMTP 发送
 *   - 邮件内容包含 6 位数字验证码，有效期 10 分钟
 *
 * 设计：单例模式，SMTP 连接参数从 config.json 读取。
 *       内部实现了一个完整的 SMTP 客户端（支持 SSL/TLS）。
 */
#pragma once

#include <string>

class EmailSender {
public:
    static EmailSender& instance(); // 获取单例实例

    // 发送验证码邮件：连接 SMTP 服务器 → 登录 → 发送邮件
    bool sendVerificationEmail(const std::string& to_email, const std::string& code);
    // 生成指定位数的随机数字验证码（默认 6 位）
    static std::string generateVerificationCode(int length = 6);

private:
    EmailSender() = default;
    ~EmailSender() = default;
    EmailSender(const EmailSender&) = delete;
    EmailSender& operator=(const EmailSender&) = delete;
};