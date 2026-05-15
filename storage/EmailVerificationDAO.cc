/*
 * EmailVerificationDAO 模块 - 邮箱验证码数据访问层实现
 *
 * 职责：封装 email_verifications 表的 SQL 操作（保存、验证、标记、清理）。
 *
 * 工作流程：
 *   1. 用户请求发送验证码 → save() 存储（10 分钟有效期），操作前自动清理过期记录
 *   2. 用户提交注册 → verifyCode() 检查是否有效 → isCodeUsed() 检查是否已使用
 *   3. 注册成功 → markCodeAsUsed() 标记为已使用
 *   4. cleanupExpired() 删除所有过期的验证码记录
 */
#include "EmailVerificationDAO.hpp"
#include "ConnectionPool.hpp"
#include "Log.hpp"
#include <mysql/mysql.h>
#include <cstring>

EmailVerificationDAO& EmailVerificationDAO::instance() {
    static EmailVerificationDAO inst;
    return inst;
}

static MYSQL* getConn() {
    return ConnectionPool::getInstance().getConnection();
}

static void releaseConn(MYSQL* conn) {
    ConnectionPool::getInstance().releaseConnection(conn);
}

// 保存验证码（10 分钟有效期），操作前自动清理过期记录
bool EmailVerificationDAO::save(const std::string& email, const std::string& code) {
    cleanupExpired();

    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "INSERT INTO email_verifications (email, verification_code, expires_at) VALUES (?, ?, DATE_ADD(NOW(), INTERVAL 10 MINUTE))";
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
    bind[0].buffer = (char*)email.c_str();
    bind[0].buffer_length = email.size();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)code.c_str();
    bind[1].buffer_length = code.size();

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    int ret = mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return (ret == 0);
}

// 验证验证码是否有效（未过期且未使用），操作前自动清理过期记录
bool EmailVerificationDAO::verifyCode(const std::string& email, const std::string& code) {
    cleanupExpired();

    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "SELECT id FROM email_verifications WHERE email = ? AND verification_code = ? AND expires_at > NOW() AND used = 0";
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
    bind[0].buffer = (char*)email.c_str();
    bind[0].buffer_length = email.size();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)code.c_str();
    bind[1].buffer_length = code.size();

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    MYSQL_BIND result_bind;
    memset(&result_bind, 0, sizeof(result_bind));
    int id;
    result_bind.buffer_type = MYSQL_TYPE_LONG;
    result_bind.buffer = &id;
    mysql_stmt_bind_result(stmt, &result_bind);
    mysql_stmt_store_result(stmt);

    bool isValid = (mysql_stmt_fetch(stmt) == 0);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return isValid;
}

// 检查验证码是否已被使用
bool EmailVerificationDAO::isCodeUsed(const std::string& email, const std::string& code) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "SELECT used FROM email_verifications WHERE email = ? AND verification_code = ?";
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
    bind[0].buffer = (char*)email.c_str();
    bind[0].buffer_length = email.size();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)code.c_str();
    bind[1].buffer_length = code.size();

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    unsigned char used = 0;
    MYSQL_BIND result_bind;
    memset(&result_bind, 0, sizeof(result_bind));
    result_bind.buffer_type = MYSQL_TYPE_TINY;
    result_bind.buffer = &used;
    result_bind.buffer_length = sizeof(used);
    mysql_stmt_bind_result(stmt, &result_bind);
    mysql_stmt_store_result(stmt);

    bool isUsed = false;
    if (mysql_stmt_fetch(stmt) == 0) {
        isUsed = (used != 0);
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return isUsed;
}

// 标记验证码为已使用（注册成功后调用，防止重复使用）
void EmailVerificationDAO::markCodeAsUsed(const std::string& email, const std::string& code) {
    MYSQL* conn = getConn();
    if (!conn) return;

    const char* sql = "UPDATE email_verifications SET used = 1 WHERE email = ? AND verification_code = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)email.c_str();
    bind[0].buffer_length = email.size();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)code.c_str();
    bind[1].buffer_length = code.size();

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return;
    }

    mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    releaseConn(conn);
}

// 清理过期验证码（DELETE WHERE expires_at <= NOW()）
void EmailVerificationDAO::cleanupExpired() {
    MYSQL* conn = getConn();
    if (!conn) return;

    const char* sql = "DELETE FROM email_verifications WHERE expires_at <= NOW()";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (stmt) {
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) == 0) {
            mysql_stmt_execute(stmt);
        }
        mysql_stmt_close(stmt);
    }

    releaseConn(conn);
}
