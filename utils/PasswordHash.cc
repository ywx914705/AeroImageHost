/*
 * PasswordHash 模块 - PBKDF2-HMAC-SHA256 密码哈希实现
 *
 * 职责：提供安全的密码哈希和验证功能。
 *
 * 存储格式：pbkdf2_sha256$迭代次数$base64(盐)$base64(哈希)
 *
 * 核心功能：
 *   - hash(): 生成密码哈希（随机 128 位盐 + 10 万次 PBKDF2 迭代）
 *   - verify(): 验证密码（从存储的哈希中解析盐，重新计算并常量时间比较）
 *   - needsRehash(): 检查是否需要升级哈希（迭代次数不足时返回 true）
 *
 * 安全特性：
 *   - 每次哈希生成随机盐（防止彩虹表攻击）
 *   - 10 万次迭代（暴力破解需要大量计算资源）
 *   - CRYPTO_memcmp 常量时间比较（防止时序攻击）
 *
 * 注意：当前项目中 Handlers.cc 使用的是固定盐 + SHA-256（兼容现有数据库），
 *       此模块是为未来升级预留的更安全方案。
 */
// PBKDF2-HMAC-SHA256 密码哈希实现
// 存储格式：pbkdf2_sha256$迭代次数$base64(盐)$base64(哈希)
#include "PasswordHash.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <vector>
#include <stdexcept>

// Base64 编码：将二进制数据转为可存储的文本
static std::string base64Encode(const unsigned char* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = static_cast<unsigned int>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<unsigned int>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<unsigned int>(data[i + 2]);
        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? table[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? table[n & 0x3F] : '=';
    }
    return result;
}

// Base64 解码：将文本还原为二进制数据
static std::vector<unsigned char> base64Decode(const std::string& input) {
    static const unsigned char decodeTable[256] = {
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,62,64,64,64,63,
        52,53,54,55,56,57,58,59,60,61,64,64,64,0,64,64,
        64,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,64,64,64,64,64,
        64,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64
    };
    std::vector<unsigned char> result;
    result.reserve(input.size() * 3 / 4);
    unsigned int accum = 0;
    int bits = 0;
    for (char c : input) {
        if (c == '=') break;
        unsigned char val = decodeTable[static_cast<unsigned char>(c)];
        if (val == 64) continue;
        accum = (accum << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<unsigned char>((accum >> bits) & 0xFF));
        }
    }
    return result;
}

// 迭代次数：10万次，暴力破解需要大量计算资源
static const int PBKDF2_ITERATIONS = 100000;
static const int SALT_LEN = 16;   // 128位随机盐
static const int HASH_LEN = 32;   // 256位输出哈希

// 生成密码哈希：随机盐 + PBKDF2 迭代 → 存储格式字符串
std::string PasswordHash::hash(const std::string& password) {
    unsigned char salt[SALT_LEN];
    if (RAND_bytes(salt, SALT_LEN) != 1) {
        throw std::runtime_error("生成随机盐失败");
    }

    unsigned char hash[HASH_LEN];
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            salt, SALT_LEN,
            PBKDF2_ITERATIONS,
            EVP_sha256(),
            HASH_LEN, hash) != 1) {
        throw std::runtime_error("PBKDF2 failed");
    }

    std::string saltB64 = base64Encode(salt, SALT_LEN);
    std::string hashB64 = base64Encode(hash, HASH_LEN);

    return "pbkdf2_sha256$" + std::to_string(PBKDF2_ITERATIONS) + "$" + saltB64 + "$" + hashB64;
}

// 验证密码：从存储的哈希中解析盐和迭代次数，重新计算并常量时间比较
bool PasswordHash::verify(const std::string& password, const std::string& storedHash) {
    // 格式：pbkdf2_sha256$iterations$base64salt$base64hash
    if (storedHash.find("pbkdf2_sha256$") != 0) {
        return false;
    }

    size_t pos1 = storedHash.find('$', 14);
    if (pos1 == std::string::npos) return false;
    size_t pos2 = storedHash.find('$', pos1 + 1);
    if (pos2 == std::string::npos) return false;

    int iterations = std::stoi(storedHash.substr(14, pos1 - 14));
    auto salt = base64Decode(storedHash.substr(pos1 + 1, pos2 - pos1 - 1));
    auto expectedHash = base64Decode(storedHash.substr(pos2 + 1));

    if (salt.size() != SALT_LEN || expectedHash.size() != HASH_LEN) return false;

    // 用存储的盐和迭代次数重新计算哈希
    unsigned char computedHash[HASH_LEN];
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            salt.data(), static_cast<int>(salt.size()),
            iterations, EVP_sha256(), HASH_LEN, computedHash) != 1) {
        return false;
    }

    // 常量时间比较，防止时序攻击
    return CRYPTO_memcmp(computedHash, expectedHash.data(), HASH_LEN) == 0;
}

// 检查是否需要重新哈希（迭代次数不够时升级）
bool PasswordHash::needsRehash(const std::string& storedHash) {
    if (storedHash.find("pbkdf2_sha256$") != 0) return true;
    size_t pos1 = storedHash.find('$', 14);
    if (pos1 == std::string::npos) return true;
    int iterations = std::stoi(storedHash.substr(14, pos1 - 14));
    return iterations < PBKDF2_ITERATIONS;
}
