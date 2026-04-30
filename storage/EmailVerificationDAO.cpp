#include "EmailVerificationDAO.hpp"
#include "ConnectionPool.hpp"
#include "Log.hpp"
#include "Config.hpp"
#include <mysql.h>
#include <vector>
#include <random>
#include <ctime>
#include <chrono>

EmailVerificationDAO& EmailVerificationDAO::instance() {
    static EmailVerificationDAO inst;
    return inst;
}

bool EmailVerificationDAO::save(const std::string& email, const std::string& code) {
    cleanupExpired(); // 清理过期的验证码

    MYSQL* conn = ConnectionPool::getInstance().getConnection();
    if (!conn) {
        LOG_ERROR("EmailVerificationDAO::save: failed to get database connection");
        return false;
    }

    const char* sql = "INSERT INTO email_verifications (email, verification_code, expires_at) VALUES (?, ?, DATE_ADD(NOW(), INTERVAL 10 MINUTE))";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        ConnectionPool::getInstance().releaseConnection(conn);
        LOG_ERROR("EmailVerificationDAO::save: mysql_stmt_init failed");
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        LOG_ERROR("EmailVerificationDAO::save: mysql_stmt_prepare failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
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
        LOG_ERROR("EmailVerificationDAO::save: mysql_stmt_bind_param failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR("EmailVerificationDAO::save: mysql_stmt_execute failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
        return false;
    }

    mysql_stmt_close(stmt);
    ConnectionPool::getInstance().releaseConnection(conn);

    LOG_INFO("Saved verification code for email: " + email);
    return true;
}

bool EmailVerificationDAO::verifyCode(const std::string& email, const std::string& code) {
    cleanupExpired(); // 清理过期的验证码

    MYSQL* conn = ConnectionPool::getInstance().getConnection();
    if (!conn) {
        LOG_ERROR("EmailVerificationDAO::verifyCode: failed to get database connection");
        return false;
    }

    const char* sql = "SELECT id FROM email_verifications WHERE email = ? AND verification_code = ? AND expires_at > NOW() AND used = 0";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        ConnectionPool::getInstance().releaseConnection(conn);
        LOG_ERROR("EmailVerificationDAO::verifyCode: mysql_stmt_init failed");
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        LOG_ERROR("EmailVerificationDAO::verifyCode: mysql_stmt_prepare failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
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
        LOG_ERROR("EmailVerificationDAO::verifyCode: mysql_stmt_bind_param failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR("EmailVerificationDAO::verifyCode: mysql_stmt_execute failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
        return false;
    }

    MYSQL_BIND result_bind;
    memset(&result_bind, 0, sizeof(result_bind));
    int id;
    result_bind.buffer_type = MYSQL_TYPE_LONG;
    result_bind.buffer = &id;

    if (mysql_stmt_bind_result(stmt, &result_bind) != 0) {
        LOG_ERROR("EmailVerificationDAO::verifyCode: mysql_stmt_bind_result failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
        return false;
    }

    bool isValid = false;
    if (mysql_stmt_fetch(stmt) == 0) {
        isValid = true;
    }

    mysql_stmt_close(stmt);
    ConnectionPool::getInstance().releaseConnection(conn);

    if (isValid) {
        LOG_INFO("Verification code verified successfully for email: " + email);
    } else {
        LOG_WARN("Invalid or expired verification code for email: " + email);
    }

    return isValid;
}

bool EmailVerificationDAO::isCodeUsed(const std::string& email, const std::string& code) {
    MYSQL* conn = ConnectionPool::getInstance().getConnection();
    if (!conn) {
        LOG_ERROR("EmailVerificationDAO::isCodeUsed: failed to get database connection");
        return false;
    }

    const char* sql = "SELECT used FROM email_verifications WHERE email = ? AND verification_code = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        ConnectionPool::getInstance().releaseConnection(conn);
        LOG_ERROR("EmailVerificationDAO::isCodeUsed: mysql_stmt_init failed");
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        LOG_ERROR("EmailVerificationDAO::isCodeUsed: mysql_stmt_prepare failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
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
        LOG_ERROR("EmailVerificationDAO::isCodeUsed: mysql_stmt_bind_param failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR("EmailVerificationDAO::isCodeUsed: mysql_stmt_execute failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
        return false;
    }

    MYSQL_BIND result_bind;
    memset(&result_bind, 0, sizeof(result_bind));
    bool used = false;
    result_bind.buffer_type = MYSQL_TYPE_TINY;
    result_bind.buffer = &used;
    result_bind.buffer_length = sizeof(bool);

    if (mysql_stmt_bind_result(stmt, &result_bind) != 0) {
        LOG_ERROR("EmailVerificationDAO::isCodeUsed: mysql_stmt_bind_result failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
        return false;
    }

    bool isUsed = false;
    if (mysql_stmt_fetch(stmt) == 0) {
        isUsed = used;
    }

    mysql_stmt_close(stmt);
    ConnectionPool::getInstance().releaseConnection(conn);

    return isUsed;
}

void EmailVerificationDAO::markCodeAsUsed(const std::string& email, const std::string& code) {
    MYSQL* conn = ConnectionPool::getInstance().getConnection();
    if (!conn) {
        LOG_ERROR("EmailVerificationDAO::markCodeAsUsed: failed to get database connection");
        return;
    }

    const char* sql = "UPDATE email_verifications SET used = 1 WHERE email = ? AND verification_code = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        ConnectionPool::getInstance().releaseConnection(conn);
        LOG_ERROR("EmailVerificationDAO::markCodeAsUsed: mysql_stmt_init failed");
        return;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        LOG_ERROR("EmailVerificationDAO::markCodeAsUsed: mysql_stmt_prepare failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
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
        LOG_ERROR("EmailVerificationDAO::markCodeAsUsed: mysql_stmt_bind_param failed: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        ConnectionPool::getInstance().releaseConnection(conn);
        return;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR("EmailVerificationDAO::markCodeAsUsed: mysql_stmt_execute failed: " + std::string(mysql_stmt_error(stmt)));
    } else {
        LOG_INFO("Marked verification code as used for email: " + email);
    }

    mysql_stmt_close(stmt);
    ConnectionPool::getInstance().releaseConnection(conn);
}

void EmailVerificationDAO::cleanupExpired() {
    MYSQL* conn = ConnectionPool::getInstance().getConnection();
    if (!conn) {
        LOG_ERROR("EmailVerificationDAO::cleanupExpired: failed to get database connection");
        return;
    }

    const char* sql = "DELETE FROM email_verifications WHERE expires_at <= NOW()";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (stmt) {
        if (mysql_stmt_prepare(stmt, sql, strlen(sql)) == 0) {
            if (mysql_stmt_execute(stmt) == 0) {
                int affected_rows = mysql_stmt_affected_rows(stmt);
                if (affected_rows > 0) {
                    LOG_INFO("Cleaned up " + std::to_string(affected_rows) + " expired verification codes");
                }
            } else {
                LOG_ERROR("EmailVerificationDAO::cleanupExpired: mysql_stmt_execute failed: " + std::string(mysql_stmt_error(stmt)));
            }
        } else {
            LOG_ERROR("EmailVerificationDAO::cleanupExpired: mysql_stmt_prepare failed: " + std::string(mysql_stmt_error(stmt)));
        }
        mysql_stmt_close(stmt);
    }

    ConnectionPool::getInstance().releaseConnection(conn);
}