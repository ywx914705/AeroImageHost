#include "ConnectionPool.hpp"
#include "Log.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <mysql/mysql.h>
#include <DBManager.hpp>
#include <openssl/evp.h>
#include <random>
#include <sstream>
#include <iomanip>

DBManager &DBManager::getInstance() {
  static DBManager instance;
  return instance;
}

bool DBManager::connect(const std::string &host, const std::string &user,
                        const std::string &passwd, const std::string &db,
                        unsigned int port) {
  (void)host;
  (void)user;
  (void)passwd;
  (void)db;
  (void)port;
  MYSQL *conn = ConnectionPool::getInstance().getConnection();
  if (!conn) {
    error_ = "Failed to get connection from pool";
    return false;
  }
  ConnectionPool::getInstance().releaseConnection(conn);
  return true;
}

MYSQL *DBManager::getConnection() {
  return ConnectionPool::getInstance().getConnection();
}

void DBManager::releaseConnection(MYSQL *conn) {
  ConnectionPool::getInstance().releaseConnection(conn);
}

// 以下函数用于即时通讯功能，图床项目暂不需要
// 保留代码框架以便未来扩展

bool DBManager::queryAccount(const std::string &account,
                             const std::string &password, std::string &username,
                             std::string &avatarUrl) {
  MYSQL *conn = getConnection();
  if (!conn) {
    error_ = "No database connection";
    return false;
  }

  // 注意：users 表没有 username 和 avatar_url 字段，此函数暂不实现
  error_ = "Not implemented for imagehost project";
  releaseConnection(conn);
  return false;
}

bool DBManager::queryUsernameByAccount(const std::string &account,
                                       std::string &username) {
  MYSQL *conn = getConnection();
  if (!conn) {
    error_ = "No database connection";
    return false;
  }
  error_ = "Not implemented for imagehost project";
  releaseConnection(conn);
  return false;
}

bool DBManager::updateUsername(const std::string &account,
                               const std::string &new_username) {
  MYSQL *conn = getConnection();
  if (!conn) {
    error_ = "No database connection";
    return false;
  }
  error_ = "Not implemented for imagehost project";
  releaseConnection(conn);
  return false;
}

std::string DBManager::queryUserAvatar(const std::string &account) {
  MYSQL *conn = getConnection();
  if (!conn) {
    error_ = "No database connection";
    return "";
  }
  error_ = "Not implemented for imagehost project";
  releaseConnection(conn);
  return "";
}

bool DBManager::updateUserAvatar(const std::string &account,
                                 const std::string &avatarUrl) {
  MYSQL *conn = getConnection();
  if (!conn) {
    error_ = "No database connection";
    return false;
  }
  error_ = "Not implemented for imagehost project";
  releaseConnection(conn);
  return false;
}

std::string DBManager::getError() const { return error_; }

std::string DBManager::generateSalt() {
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    std::string salt;
    salt.reserve(16);
    for (int i = 0; i < 16; ++i) salt += charset[dis(gen)];
    return salt;
}

std::string DBManager::hashPassword(const std::string& password, const std::string& salt) {
    std::string salted = salt + password;
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, salted.c_str(), salted.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &len) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < len; i++) {
        ss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return ss.str();
}
