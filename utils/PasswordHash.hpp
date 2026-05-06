#pragma once
#include <string>

// PBKDF2-HMAC-SHA256 密码哈希工具
// 存储格式：pbkdf2_sha256$迭代次数$base64(盐)$base64(哈希)
namespace PasswordHash {
    // 生成密码哈希（随机盐 + 10万次迭代）
    std::string hash(const std::string& password);

    // 验证密码是否匹配存储的哈希
    bool verify(const std::string& password, const std::string& storedHash);

    // 检查是否需要升级哈希（迭代次数不足时返回 true）
    bool needsRehash(const std::string& storedHash);
}
