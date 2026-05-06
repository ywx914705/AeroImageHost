/*
 * UsersDAO 模块 - 用户数据访问层实现
 *
 * 职责：封装 users 表的 SQL 操作（注册、登录、邮箱检查、统计）。
 *
 * 工作流程：
 *   1. registerUser() → INSERT INTO users (account, password_hash)
 *   2. loginUser() → SELECT id, password_hash FROM users WHERE account = ?
 *   3. emailExists() → SELECT id FROM users WHERE email = ?（事务内复用连接）
 *   4. registerUserWithEmail() → INSERT INTO users (account, password_hash, email)（事务内复用连接）
 *   5. getUserCount() → SELECT COUNT(*) FROM users
 */
#include "UsersDAO.hpp"
#include "ConnectionPool.hpp"
#include "Log.hpp"
#include <cstring>

UsersDAO& UsersDAO::instance() {
    static UsersDAO inst;
    return inst;
}

static MYSQL* getConn() {
    return ConnectionPool::getInstance().getConnection();
}

static void releaseConn(MYSQL* conn) {
    ConnectionPool::getInstance().releaseConnection(conn);
}

// 注册普通用户（account + passwordHash），account 重复时返回 false
bool UsersDAO::registerUser(const std::string& account, const std::string& passwordHash) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "INSERT INTO users (account, password_hash) VALUES (?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return false;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)account.c_str();
    bind[0].buffer_length = account.size();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)passwordHash.c_str();
    bind[1].buffer_length = passwordHash.size();
    mysql_stmt_bind_param(stmt, bind);

    int ret = mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return (ret == 0);
}

// 登录验证：查询 user_id 和 password_hash
// 成功返回 user_id (>0)，账号不存在返回 0，错误返回 -1
int UsersDAO::loginUser(const std::string& account, std::string& passwordHash) {
    MYSQL* conn = getConn();
    if (!conn) return -1;

    const char* sql = "SELECT id, password_hash FROM users WHERE account = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return -1;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return -1;
    }

    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = (char*)account.c_str();
    param.buffer_length = account.size();
    mysql_stmt_bind_param(stmt, &param);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return -1;
    }

    int user_id = 0;
    char storedHashBuf[256] = {0};
    MYSQL_BIND result[2];
    memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = &user_id;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = storedHashBuf;
    result[1].buffer_length = sizeof(storedHashBuf) - 1;
    mysql_stmt_bind_result(stmt, result);
    mysql_stmt_store_result(stmt);

    int fetchResult = mysql_stmt_fetch(stmt);
    mysql_stmt_close(stmt);
    releaseConn(conn);

    if (fetchResult != 0) return 0;
    passwordHash = std::string(storedHashBuf);
    return user_id;
}

// 检查邮箱是否已注册（事务内使用，复用传入的连接）
bool UsersDAO::emailExists(MYSQL* conn, const std::string& email) {
    const char* sql = "SELECT id FROM users WHERE email = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return true;

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return true;
    }

    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = (char*)email.c_str();
    param.buffer_length = email.size();
    mysql_stmt_bind_param(stmt, &param);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return true;
    }

    int user_id = 0;
    MYSQL_BIND result;
    memset(&result, 0, sizeof(result));
    result.buffer_type = MYSQL_TYPE_LONG;
    result.buffer = &user_id;
    mysql_stmt_bind_result(stmt, &result);
    mysql_stmt_store_result(stmt);

    bool exists = (mysql_stmt_fetch(stmt) == 0);
    mysql_stmt_close(stmt);
    return exists;
}

// 邮箱注册（事务内使用，复用传入的连接）
// 返回：0=成功, -1=插入失败(账号/邮箱重复), -2=其他错误
int UsersDAO::registerUserWithEmail(MYSQL* conn, const std::string& account,
                                     const std::string& passwordHash, const std::string& email) {
    const char* sql = "INSERT INTO users (account, password_hash, email) VALUES (?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return -2;

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return -2;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)account.c_str();
    bind[0].buffer_length = account.size();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)passwordHash.c_str();
    bind[1].buffer_length = passwordHash.size();
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)email.c_str();
    bind[2].buffer_length = email.size();
    mysql_stmt_bind_param(stmt, bind);

    int ret = mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    return (ret == 0) ? 0 : -1;
}

// 获取注册用户总数
int UsersDAO::getUserCount() {
    MYSQL* conn = getConn();
    if (!conn) return 0;

    const char* sql = "SELECT COUNT(*) FROM users";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return 0;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return 0;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return 0;
    }

    int count = 0;
    MYSQL_BIND result;
    memset(&result, 0, sizeof(result));
    result.buffer_type = MYSQL_TYPE_LONG;
    result.buffer = &count;
    mysql_stmt_bind_result(stmt, &result);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return count;
}
