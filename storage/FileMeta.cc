#include "FileMeta.hpp"
#include "ConnectionPool.hpp"
#include "MinIOClient.hpp"
#include "Log.hpp"
#include <mysql/mysql.h>
#include <cstring>
#include <ctime>

FileMetaDAO& FileMetaDAO::instance() {
    static FileMetaDAO inst;
    return inst;
}

static MYSQL* getConn() {
    return ConnectionPool::getInstance().getConnection();
}

static void releaseConn(MYSQL* conn) {
    ConnectionPool::getInstance().releaseConnection(conn);
}

bool FileMetaDAO::save(const FileMeta& meta) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "INSERT INTO files (file_id, user_id, filename, size, mime_type, width, height, is_public, upload_time) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        LOG_ERROR("mysql_stmt_prepare: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    MYSQL_BIND bind[9];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)meta.file_id.c_str();
    bind[0].buffer_length = meta.file_id.size();

    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (void*)&meta.user_id;

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)meta.filename.c_str();
    bind[2].buffer_length = meta.filename.size();

    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = (void*)&meta.size;

    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)meta.mime_type.c_str();
    bind[4].buffer_length = meta.mime_type.size();

    bind[5].buffer_type = MYSQL_TYPE_LONG;
    bind[5].buffer = (void*)&meta.width;

    bind[6].buffer_type = MYSQL_TYPE_LONG;
    bind[6].buffer = (void*)&meta.height;

    int is_public_int = meta.is_public ? 1 : 0;
    bind[7].buffer_type = MYSQL_TYPE_LONG;
    bind[7].buffer = (void*)&is_public_int;

    time_t upload_time = meta.upload_time;
    bind[8].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[8].buffer = (void*)&upload_time;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        LOG_ERROR("mysql_stmt_bind_param: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR("mysql_stmt_execute: " + std::string(mysql_stmt_error(stmt)));
        MinIOClient::instance().deleteObject(meta.file_id);
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return true;
}

FileMeta FileMetaDAO::get(const std::string& file_id) {
    FileMeta meta;
    meta.file_id = "";
    MYSQL* conn = getConn();
    if (!conn) return meta;

    const char* sql = "SELECT file_id, user_id, filename, size, mime_type, width, height, upload_time, is_public, allow_domains FROM files WHERE file_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return meta;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return meta;
    }

    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = (char*)file_id.c_str();
    param.buffer_length = file_id.size();
    mysql_stmt_bind_param(stmt, &param);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return meta;
    }

    char file_id_buf[64] = {0}, filename_buf[512] = {0}, mime_buf[256] = {0}, allow_buf[512] = {0};
    int user_id = 0, width = 0, height = 0, is_public_int = 0;
    long long size = 0, upload_time = 0;

    MYSQL_BIND result[10];
    memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_STRING;
    result[0].buffer = file_id_buf;
    result[0].buffer_length = sizeof(file_id_buf);
    result[1].buffer_type = MYSQL_TYPE_LONG;
    result[1].buffer = &user_id;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = filename_buf;
    result[2].buffer_length = sizeof(filename_buf);
    result[3].buffer_type = MYSQL_TYPE_LONGLONG;
    result[3].buffer = &size;
    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer = mime_buf;
    result[4].buffer_length = sizeof(mime_buf);
    result[5].buffer_type = MYSQL_TYPE_LONG;
    result[5].buffer = &width;
    result[6].buffer_type = MYSQL_TYPE_LONG;
    result[6].buffer = &height;
    result[7].buffer_type = MYSQL_TYPE_LONGLONG;
    result[7].buffer = &upload_time;
    result[8].buffer_type = MYSQL_TYPE_LONG;
    result[8].buffer = &is_public_int;
    result[9].buffer_type = MYSQL_TYPE_STRING;
    result[9].buffer = allow_buf;
    result[9].buffer_length = sizeof(allow_buf);

    mysql_stmt_bind_result(stmt, result);
    mysql_stmt_store_result(stmt);

    if (mysql_stmt_fetch(stmt) == 0) {
        meta.file_id = file_id_buf;
        meta.user_id = user_id;
        meta.filename = urlDecode(filename_buf);
        meta.size = size;
        meta.mime_type = mime_buf;
        meta.width = width;
        meta.height = height;
        meta.upload_time = upload_time;
        meta.is_public = is_public_int;
        meta.allow_domains = allow_buf;
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return meta;
}

std::vector<FileMeta> FileMetaDAO::listByUser(int user_id, int offset, int limit) {
    return listByUserWithSearch(user_id, "", offset, limit);
}

int FileMetaDAO::countByUser(int user_id) {
    return countByUserWithSearch(user_id, "");
}

bool FileMetaDAO::del(const std::string& file_id) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "DELETE FROM files WHERE file_id = ?";
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

    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = (char*)file_id.c_str();
    param.buffer_length = file_id.size();
    mysql_stmt_bind_param(stmt, &param);

    int ret = mysql_stmt_execute(stmt);
    bool success = (ret == 0 && mysql_stmt_affected_rows(stmt) > 0);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return success;
}

bool FileMetaDAO::updatePublic(const std::string& file_id, bool is_public) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "UPDATE files SET is_public = ? WHERE file_id = ?";
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

    int is_public_int = is_public ? 1 : 0;
    MYSQL_BIND param[2];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].buffer = &is_public_int;
    param[1].buffer_type = MYSQL_TYPE_STRING;
    param[1].buffer = (char*)file_id.c_str();
    param[1].buffer_length = file_id.size();
    mysql_stmt_bind_param(stmt, param);

    int ret = mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return (ret == 0);
}

int FileMetaDAO::cleanupOrphanedFiles() {
    MYSQL* conn = getConn();
    if (!conn) return 0;

    // 首先获取所有文件ID
    const char* sql = "SELECT file_id FROM files";
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

    MYSQL_BIND result;
    memset(&result, 0, sizeof(result));
    char file_id_buf[64] = {0};
    result.buffer_type = MYSQL_TYPE_STRING;
    result.buffer = file_id_buf;
    result.buffer_length = sizeof(file_id_buf);

    mysql_stmt_bind_result(stmt, &result);
    mysql_stmt_store_result(stmt);

    int cleaned_count = 0;
    while (mysql_stmt_fetch(stmt) == 0) {
        std::string file_id = file_id_buf;
        if (!MinIOClient::instance().objectExists(file_id)) {
            LOG_WARN("Cleaning up orphaned file metadata: " + file_id);
            if (del(file_id)) {
                cleaned_count++;
            }
        }
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return cleaned_count;
}

bool FileMetaDAO::incrementViewCount(const std::string& file_id) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "UPDATE files SET view_count = view_count + 1 WHERE file_id = ?";
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

    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = (char*)file_id.c_str();
    param.buffer_length = file_id.size();
    mysql_stmt_bind_param(stmt, &param);

    int ret = mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return (ret == 0);
}

std::vector<FileMeta> FileMetaDAO::listByUserWithSearch(int user_id, const std::string& keyword, int offset, int limit) {
    std::vector<FileMeta> result;
    MYSQL* conn = getConn();
    if (!conn) { LOG_ERROR("listByUserWithSearch: no connection"); return result; }

    std::string sql;
    if (keyword.empty()) {
        sql = "SELECT file_id, filename, size, mime_type, width, height, upload_time, is_public FROM files WHERE user_id = ? ORDER BY upload_time DESC LIMIT ?, ?";
    } else {
        // 搜索文件名（模糊匹配）
        sql = "SELECT file_id, filename, size, mime_type, width, height, upload_time, is_public FROM files WHERE user_id = ? AND filename LIKE ? ORDER BY upload_time DESC LIMIT ?, ?";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        LOG_ERROR("listByUserWithSearch: mysql_stmt_init failed");
        releaseConn(conn);
        return result;
    }
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
        LOG_ERROR("listByUserWithSearch prepare: " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return result;
    }

    MYSQL_BIND param[4];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].buffer = &user_id;

    if (!keyword.empty()) {
        std::string searchPattern = "%" + keyword + "%";
        param[1].buffer_type = MYSQL_TYPE_STRING;
        param[1].buffer = (char*)searchPattern.c_str();
        param[1].buffer_length = searchPattern.length();
        param[2].buffer_type = MYSQL_TYPE_LONG;
        param[2].buffer = &offset;
        param[3].buffer_type = MYSQL_TYPE_LONG;
        param[3].buffer = &limit;

        if (mysql_stmt_bind_param(stmt, param) != 0) {
            LOG_ERROR("listByUserWithSearch bind_param: " + std::string(mysql_stmt_error(stmt)));
        }
    } else {
        param[1].buffer_type = MYSQL_TYPE_LONG;
        param[1].buffer = &offset;
        param[2].buffer_type = MYSQL_TYPE_LONG;
        param[2].buffer = &limit;

        if (mysql_stmt_bind_param(stmt, param) != 0) {
            LOG_ERROR("listByUserWithSearch bind_param: " + std::string(mysql_stmt_error(stmt)));
        }
    }

    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR("listByUserWithSearch query failed for user_id=" + std::to_string(user_id) + ": " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return result;
    }

    MYSQL_BIND resultBind[8];
    memset(resultBind, 0, sizeof(resultBind));
    char file_id_buf[64] = {0}, filename_buf[512] = {0}, mime_buf[256] = {0};
    long long size;
    int width, height, is_public_int;
    long long upload_time;

    resultBind[0].buffer_type = MYSQL_TYPE_STRING;
    resultBind[0].buffer = file_id_buf;
    resultBind[0].buffer_length = sizeof(file_id_buf);
    resultBind[1].buffer_type = MYSQL_TYPE_STRING;
    resultBind[1].buffer = filename_buf;
    resultBind[1].buffer_length = sizeof(filename_buf);
    resultBind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    resultBind[2].buffer = &size;
    resultBind[3].buffer_type = MYSQL_TYPE_STRING;
    resultBind[3].buffer = mime_buf;
    resultBind[3].buffer_length = sizeof(mime_buf);
    resultBind[4].buffer_type = MYSQL_TYPE_LONG;
    resultBind[4].buffer = &width;
    resultBind[5].buffer_type = MYSQL_TYPE_LONG;
    resultBind[5].buffer = &height;
    resultBind[6].buffer_type = MYSQL_TYPE_LONGLONG;
    resultBind[6].buffer = &upload_time;
    resultBind[7].buffer_type = MYSQL_TYPE_LONG;
    resultBind[7].buffer = &is_public_int;

    if (mysql_stmt_bind_result(stmt, resultBind) != 0) {
        LOG_ERROR("listByUserWithSearch bind_result: " + std::string(mysql_stmt_error(stmt)));
    }

    unsigned long actual_len[8];
    bool is_null[8] = {false};
    bool bind_error[8] = {false};
    for (int i = 0; i < 8; i++) {
        resultBind[i].length = &actual_len[i];
        resultBind[i].is_null = &is_null[i];
        resultBind[i].error = &bind_error[i];
    }

    if (mysql_stmt_store_result(stmt) != 0) {
        LOG_ERROR("listByUserWithSearch store_result: " + std::string(mysql_stmt_error(stmt)));
    }

    int fetch_count = 0;
    int fetch_result;
    while ((fetch_result = mysql_stmt_fetch(stmt)) == 0) {
        FileMeta meta;
        meta.file_id = file_id_buf;
        meta.filename = urlDecode(filename_buf);
        meta.size = size;
        meta.mime_type = mime_buf;
        meta.width = width;
        meta.height = height;
        meta.upload_time = upload_time;
        meta.is_public = is_public_int;
        meta.view_count = 0;
        result.push_back(meta);
        fetch_count++;
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return result;
}

int FileMetaDAO::countByUserWithSearch(int user_id, const std::string& keyword) {
    MYSQL* conn = getConn();
    if (!conn) return 0;

    std::string sql;
    if (keyword.empty()) {
        sql = "SELECT COUNT(*) FROM files WHERE user_id = ?";
    } else {
        sql = "SELECT COUNT(*) FROM files WHERE user_id = ? AND filename LIKE ?";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) { releaseConn(conn); return 0; }
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
        mysql_stmt_close(stmt); releaseConn(conn); return 0;
    }

    MYSQL_BIND param[2];
    memset(&param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].buffer = &user_id;

    if (!keyword.empty()) {
        std::string searchPattern = "%" + keyword + "%";
        param[1].buffer_type = MYSQL_TYPE_STRING;
        param[1].buffer = (char*)searchPattern.c_str();
        param[1].buffer_length = searchPattern.length();
        mysql_stmt_bind_param(stmt, param);
    } else {
        mysql_stmt_bind_param(stmt, param);
    }

    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR("countByUserWithSearch query failed for user_id=" + std::to_string(user_id) + ": " + std::string(mysql_stmt_error(stmt)));
        mysql_stmt_close(stmt); releaseConn(conn); return 0;
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
