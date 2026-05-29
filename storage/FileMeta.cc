/*
 * FileMetaDAO 模块 - 文件元数据数据访问层实现
 *
 * 职责：封装 files 表的所有 SQL 操作（CRUD + 分页查询 + 批量操作 + 统计）。
 *       通过 ConnectionPool 获取 MySQL 连接，使用 prepared statement 防止 SQL 注入。
 *
 * cleanupOrphanedFiles 是唯一的业务协调方法：同时操作 DB 和 MinIO。
 */
#include "FileMeta.hpp"
#include "ConnectionPool.hpp"
#include "DbGuard.hpp"
#include "MinIOClient.hpp"
#include "RedisClient.hpp"
#include "Log.hpp"
#include <mysql/mysql.h>
#include <cstring>
#include <ctime>
#include <thread>

// FileMeta 序列化为 Redis Hash 字段映射
static std::unordered_map<std::string, std::string> metaToHash(const FileMeta& m) {
    return {
        {"file_id", m.file_id},
        {"user_id", std::to_string(m.user_id)},
        {"filename", m.filename},
        {"size", std::to_string(m.size)},
        {"mime_type", m.mime_type},
        {"width", std::to_string(m.width)},
        {"height", std::to_string(m.height)},
        {"upload_time", std::to_string(static_cast<long long>(m.upload_time))},
        {"is_public", std::to_string(m.is_public ? 1 : 0)},
        {"view_count", std::to_string(m.view_count)},
        {"allow_domains", m.allow_domains}
    };
}

// 从 Redis Hash 字段映射反序列化 FileMeta
static FileMeta hashToMeta(const std::unordered_map<std::string, std::string>& h) {
    FileMeta m;
    auto get = [&](const std::string& k, const std::string& def = "") -> const std::string& {
        auto it = h.find(k);
        return (it != h.end()) ? it->second : def;
    };
    m.file_id = get("file_id");
    if (m.file_id.empty()) return m;
    try { m.user_id = std::stoi(get("user_id", "0")); } catch (...) {}
    m.filename = get("filename");
    try { m.size = std::stoll(get("size", "0")); } catch (...) {}
    m.mime_type = get("mime_type");
    try { m.width = std::stoi(get("width", "0")); } catch (...) {}
    try { m.height = std::stoi(get("height", "0")); } catch (...) {}
    try { m.upload_time = std::stoll(get("upload_time", "0")); } catch (...) {}
    m.is_public = (get("is_public", "0") == "1");
    try { m.view_count = std::stoll(get("view_count", "0")); } catch (...) {}
    m.allow_domains = get("allow_domains");
    return m;
}

static const std::string META_CACHE_PREFIX = "file_meta:";
static const int META_CACHE_TTL = 300;

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
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    int ret = mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return (ret == 0);
}

FileMeta FileMetaDAO::get(const std::string& file_id) {
    std::string cacheKey = META_CACHE_PREFIX + file_id;
    auto cached = RedisClient::instance().hgetall(cacheKey);
    if (!cached.empty()) {
        if (cached.count("__null__")) {
            FileMeta empty;
            empty.file_id = "";
            return empty;
        }
        return hashToMeta(cached);
    }

    std::string lockKey = "lock:meta:" + file_id;
    bool hasLock = RedisClient::instance().setNxEx(lockKey, "1", 3);

    if (!hasLock) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto retryCached = RedisClient::instance().hgetall(cacheKey);
        if (!retryCached.empty()) {
            if (retryCached.count("__null__")) {
                FileMeta empty;
                empty.file_id = "";
                return empty;
            }
            return hashToMeta(retryCached);
        }
    }

    FileMeta meta;
    meta.file_id = "";
    MYSQL* conn = getConn();
    if (!conn) {
        if (hasLock) RedisClient::instance().del(lockKey);
        return meta;
    }

    const char* sql = "SELECT file_id, user_id, filename, size, mime_type, width, height, upload_time, is_public, allow_domains FROM files WHERE file_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        if (hasLock) RedisClient::instance().del(lockKey);
        return meta;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        if (hasLock) RedisClient::instance().del(lockKey);
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
        if (hasLock) RedisClient::instance().del(lockKey);
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

    bool file_id_null = false, filename_null = false, mime_null = false, allow_null = false;
    result[0].is_null = &file_id_null;
    result[2].is_null = &filename_null;
    result[4].is_null = &mime_null;
    result[9].is_null = &allow_null;

    mysql_stmt_bind_result(stmt, result);
    mysql_stmt_store_result(stmt);

    if (mysql_stmt_fetch(stmt) == 0) {
        if (!file_id_null) meta.file_id = file_id_buf;
        meta.user_id = user_id;
        if (!filename_null) meta.filename = urlDecode(filename_buf);
        meta.size = size;
        if (!mime_null) meta.mime_type = mime_buf;
        meta.width = width;
        meta.height = height;
        meta.upload_time = upload_time;
        meta.is_public = is_public_int;
        if (!allow_null) meta.allow_domains = allow_buf;
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);

    if (!meta.file_id.empty()) {
        RedisClient::instance().hsetex(cacheKey, metaToHash(meta), META_CACHE_TTL);
    } else {
        std::unordered_map<std::string, std::string> nullMap = {{"__null__", "1"}};
        RedisClient::instance().hsetex(cacheKey, nullMap, 30);
    }

    if (hasLock) {
        RedisClient::instance().del(lockKey);
    }

    return meta;
}

std::vector<FileMeta> FileMetaDAO::listByUser(int user_id, int offset, int limit) {
    return listByUserWithSearch(user_id, "", offset, limit);
}

int FileMetaDAO::countByUser(int user_id) {
    return countByUserWithSearch(user_id, "");
}

bool FileMetaDAO::del(const std::string& file_id) {
    DbGuard guard;
    if (!guard) return false;
    MYSQL* conn = guard.get();

    const char* sql = "DELETE FROM files WHERE file_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
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
    // DB 删除成功后再清除缓存
    if (success) {
        RedisClient::instance().del(META_CACHE_PREFIX + file_id);
    }
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
    // DB 更新成功后再清除缓存
    if (ret == 0) {
        RedisClient::instance().del(META_CACHE_PREFIX + file_id);
    }
    return (ret == 0);
}

bool FileMetaDAO::updateAllowDomains(const std::string& file_id, const std::string& domains) {
    return false;
}

int FileMetaDAO::cleanupOrphanedFiles() {
    auto allIds = getAllFileIds();
    int cleaned_count = 0;
    for (const auto& fid : allIds) {
        if (!MinIOClient::instance().objectExists(fid)) {
            LOG_WARN("Cleaning up orphaned file metadata: " + fid);
            if (del(fid)) {
                cleaned_count++;
            }
        }
    }
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

// 批量同步浏览计数：将 Redis 中的增量计数合并写入 MySQL（事务保护）
bool FileMetaDAO::batchUpdateViewCount(const std::vector<std::pair<std::string, long long>>& updates) {
    if (updates.empty()) return true;
    MYSQL* conn = getConn();
    if (!conn) return false;

    mysql_autocommit(conn, 0);

    const char* sql = "UPDATE files SET view_count = view_count + ? WHERE file_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        mysql_autocommit(conn, 1);
        releaseConn(conn);
        return false;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        mysql_autocommit(conn, 1);
        releaseConn(conn);
        return false;
    }

    bool success = true;
    for (const auto& [fid, count] : updates) {
        MYSQL_BIND params[2];
        memset(params, 0, sizeof(params));

        long long cnt = count;
        params[0].buffer_type = MYSQL_TYPE_LONGLONG;
        params[0].buffer = &cnt;

        params[1].buffer_type = MYSQL_TYPE_STRING;
        params[1].buffer = (char*)fid.c_str();
        params[1].buffer_length = fid.size();

        mysql_stmt_bind_param(stmt, params);
        if (mysql_stmt_execute(stmt) != 0) {
            success = false;
            break;
        }
    }

    mysql_stmt_close(stmt);
    if (success) {
        mysql_commit(conn);
    } else {
        mysql_rollback(conn);
    }
    mysql_autocommit(conn, 1);
    releaseConn(conn);
    return success;
}

std::vector<FileMeta> FileMetaDAO::listByUserWithSearch(int user_id, const std::string& keyword, int offset, int limit) {
    std::vector<FileMeta> result;
    MYSQL* conn = getConn();
    if (!conn) return result;

    std::string sql;
    if (keyword.empty()) {
        sql = "SELECT file_id, filename, size, mime_type, width, height, upload_time, is_public FROM files WHERE user_id = ? ORDER BY upload_time DESC LIMIT ?, ?";
    } else {
        sql = "SELECT file_id, filename, size, mime_type, width, height, upload_time, is_public FROM files WHERE user_id = ? AND filename LIKE ? ORDER BY upload_time DESC LIMIT ?, ?";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return result;
    }
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
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
    } else {
        param[1].buffer_type = MYSQL_TYPE_LONG;
        param[1].buffer = &offset;
        param[2].buffer_type = MYSQL_TYPE_LONG;
        param[2].buffer = &limit;
    }
    mysql_stmt_bind_param(stmt, param);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return result;
    }

    MYSQL_BIND resultBind[8];
    memset(resultBind, 0, sizeof(resultBind));
    char file_id_buf[64] = {0}, filename_buf[512] = {0}, mime_buf[256] = {0};
    long long size = 0;
    int width = 0, height = 0, is_public_int = 0;
    long long upload_time = 0;

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

    // 必须在 bind_result 之前设置 is_null/length/error 指针
    unsigned long actual_len[8];
    bool is_null[8] = {false};
    bool bind_error[8] = {false};
    for (int i = 0; i < 8; i++) {
        resultBind[i].length = &actual_len[i];
        resultBind[i].is_null = &is_null[i];
        resultBind[i].error = &bind_error[i];
    }

    mysql_stmt_bind_result(stmt, resultBind);

    mysql_stmt_store_result(stmt);

    while (mysql_stmt_fetch(stmt) == 0) {
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
    memset(param, 0, sizeof(param));
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

std::pair<std::vector<FileMeta>, int> FileMetaDAO::listAndCountByUserWithSearch(int user_id, const std::string& keyword, int offset, int limit) {
    std::vector<FileMeta> result;
    int total = 0;
    MYSQL* conn = getConn();
    if (!conn) return {result, total};

    std::string sql;
    if (keyword.empty()) {
        sql = "SELECT file_id, filename, size, mime_type, width, height, upload_time, is_public FROM files WHERE user_id = ? ORDER BY upload_time DESC LIMIT ?, ?";
    } else {
        sql = "SELECT file_id, filename, size, mime_type, width, height, upload_time, is_public FROM files WHERE user_id = ? AND filename LIKE ? ORDER BY upload_time DESC LIMIT ?, ?";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return {result, total};
    }
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return {result, total};
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
    } else {
        param[1].buffer_type = MYSQL_TYPE_LONG;
        param[1].buffer = &offset;
        param[2].buffer_type = MYSQL_TYPE_LONG;
        param[2].buffer = &limit;
    }
    mysql_stmt_bind_param(stmt, param);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return {result, total};
    }

    MYSQL_BIND resultBind[8];
    memset(resultBind, 0, sizeof(resultBind));
    char file_id_buf[64] = {0}, filename_buf[512] = {0}, mime_buf[256] = {0};
    long long size = 0;
    int width = 0, height = 0, is_public_int = 0;
    long long upload_time = 0;

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

    unsigned long actual_len[8];
    bool is_null[8] = {false};
    bool bind_error[8] = {false};
    for (int i = 0; i < 8; i++) {
        resultBind[i].length = &actual_len[i];
        resultBind[i].is_null = &is_null[i];
        resultBind[i].error = &bind_error[i];
    }

    mysql_stmt_bind_result(stmt, resultBind);
    mysql_stmt_store_result(stmt);

    while (mysql_stmt_fetch(stmt) == 0) {
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
    }

    mysql_stmt_close(stmt);

    total = countByUserWithSearch(user_id, keyword);

    releaseConn(conn);
    return {result, total};
}

std::vector<std::string> FileMetaDAO::getValidFileIds(const std::vector<std::string>& file_ids, int user_id) {
    std::vector<std::string> valid_ids;
    MYSQL* conn = getConn();
    if (!conn || file_ids.empty()) return valid_ids;

    std::string placeholders;
    for (size_t i = 0; i < file_ids.size(); ++i) {
        if (i > 0) placeholders += ",";
        placeholders += "?";
    }
    std::string sql = "SELECT file_id FROM files WHERE file_id IN (" + placeholders + ") AND user_id = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return valid_ids;
    }
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return valid_ids;
    }

    std::vector<MYSQL_BIND> params(file_ids.size() + 1);
    memset(params.data(), 0, params.size() * sizeof(MYSQL_BIND));
    std::vector<std::string> id_copies(file_ids);
    for (size_t i = 0; i < file_ids.size(); ++i) {
        params[i].buffer_type = MYSQL_TYPE_STRING;
        params[i].buffer = (char*)id_copies[i].c_str();
        params[i].buffer_length = id_copies[i].size();
    }
    params[file_ids.size()].buffer_type = MYSQL_TYPE_LONG;
    params[file_ids.size()].buffer = &user_id;
    mysql_stmt_bind_param(stmt, params.data());

    if (mysql_stmt_execute(stmt) == 0) {
        char fid_buf[64] = {0};
        MYSQL_BIND result;
        memset(&result, 0, sizeof(result));
        result.buffer_type = MYSQL_TYPE_STRING;
        result.buffer = fid_buf;
        result.buffer_length = sizeof(fid_buf);
        mysql_stmt_bind_result(stmt, &result);
        mysql_stmt_store_result(stmt);
        while (mysql_stmt_fetch(stmt) == 0) {
            fid_buf[sizeof(fid_buf) - 1] = '\0';
            valid_ids.push_back(fid_buf);
        }
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return valid_ids;
}

bool FileMetaDAO::deleteFilesBatch(const std::vector<std::string>& valid_file_ids) {
    if (valid_file_ids.empty()) return true;

    MYSQL* conn = getConn();
    if (!conn) return false;

    std::string placeholders;
    for (size_t i = 0; i < valid_file_ids.size(); ++i) {
        if (i > 0) placeholders += ",";
        placeholders += "?";
    }
    std::string sql = "DELETE FROM files WHERE file_id IN (" + placeholders + ")";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return false;
    }
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    std::vector<MYSQL_BIND> params(valid_file_ids.size());
    memset(params.data(), 0, params.size() * sizeof(MYSQL_BIND));
    std::vector<std::string> id_copies(valid_file_ids);
    for (size_t i = 0; i < valid_file_ids.size(); ++i) {
        params[i].buffer_type = MYSQL_TYPE_STRING;
        params[i].buffer = (char*)id_copies[i].c_str();
        params[i].buffer_length = id_copies[i].size();
    }
    mysql_stmt_bind_param(stmt, params.data());

    int ret = mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    // DB 删除成功后再批量清除缓存
    if (ret == 0) {
        for (const auto& fid : valid_file_ids) {
            RedisClient::instance().del(META_CACHE_PREFIX + fid);
        }
    }
    return (ret == 0);
}

std::vector<std::string> FileMetaDAO::getAllFileIds() {
    std::vector<std::string> result;
    MYSQL* conn = getConn();
    if (!conn) return result;

    const char* sql = "SELECT file_id FROM files";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return result;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return result;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return result;
    }

    MYSQL_BIND bind;
    memset(&bind, 0, sizeof(bind));
    char file_id_buf[64] = {0};
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = file_id_buf;
    bind.buffer_length = sizeof(file_id_buf);
    mysql_stmt_bind_result(stmt, &bind);
    mysql_stmt_store_result(stmt);

    while (mysql_stmt_fetch(stmt) == 0) {
        file_id_buf[sizeof(file_id_buf) - 1] = '\0';
        result.push_back(file_id_buf);
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return result;
}

void FileMetaDAO::getFileStats(int& total_files, int& total_images, long long& total_size) {
    total_files = 0;
    total_images = 0;
    total_size = 0;

    MYSQL* conn = getConn();
    if (!conn) return;

    const char* sql = "SELECT COUNT(*), COUNT(CASE WHEN mime_type LIKE 'image/%' THEN 1 END), COALESCE(SUM(size), 0) FROM files";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (stmt && mysql_stmt_prepare(stmt, sql, strlen(sql)) == 0 && mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND result[3];
        memset(result, 0, sizeof(result));
        result[0].buffer_type = MYSQL_TYPE_LONG;
        result[0].buffer = &total_files;
        result[1].buffer_type = MYSQL_TYPE_LONG;
        result[1].buffer = &total_images;
        result[2].buffer_type = MYSQL_TYPE_LONGLONG;
        result[2].buffer = &total_size;
        mysql_stmt_bind_result(stmt, result);
        mysql_stmt_store_result(stmt);
        mysql_stmt_fetch(stmt);
    }
    if (stmt) mysql_stmt_close(stmt);

    releaseConn(conn);
}

/* 获取用户已使用的存储空间 */
long long FileMetaDAO::getUserStorageUsage(int user_id) {
    long long total_size = 0;
    MYSQL* conn = getConn();
    if (!conn) return 0;

    const char* sql = "SELECT COALESCE(SUM(size), 0) FROM files WHERE user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt || mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        releaseConn(conn);
        return 0;
    }

    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    param.buffer_type = MYSQL_TYPE_LONG;
    param.buffer = &user_id;
    mysql_stmt_bind_param(stmt, &param);

    if (mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND result;
        memset(&result, 0, sizeof(result));
        result.buffer_type = MYSQL_TYPE_LONGLONG;
        result.buffer = &total_size;
        mysql_stmt_bind_result(stmt, &result);
        mysql_stmt_store_result(stmt);
        mysql_stmt_fetch(stmt);
    }
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return total_size;
}

/* 设置水印配置 */
bool FileMetaDAO::setWatermark(const std::string& file_id, const std::string& text,
                               const std::string& position, int opacity) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "UPDATE files SET watermark_text=?, watermark_position=?, watermark_opacity=? WHERE file_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt || mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    MYSQL_BIND params[4];
    memset(params, 0, sizeof(params));

    std::string text_val = text;
    std::string pos_val = position;
    int op_val = opacity;
    std::string fid = file_id;

    params[0].buffer_type = MYSQL_TYPE_STRING;
    params[0].buffer = &text_val[0];
    params[0].buffer_length = text_val.size();
    params[1].buffer_type = MYSQL_TYPE_STRING;
    params[1].buffer = &pos_val[0];
    params[1].buffer_length = pos_val.size();
    params[2].buffer_type = MYSQL_TYPE_LONG;
    params[2].buffer = &op_val;
    params[3].buffer_type = MYSQL_TYPE_STRING;
    params[3].buffer = &fid[0];
    params[3].buffer_length = fid.size();

    mysql_stmt_bind_param(stmt, params);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return ok;
}

/* 清除水印配置 */
bool FileMetaDAO::clearWatermark(const std::string& file_id) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "UPDATE files SET watermark_text=NULL, watermark_position=\"bottom-right\", watermark_opacity=50 WHERE file_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt || mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    std::string fid = file_id;
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = &fid[0];
    param.buffer_length = fid.size();

    mysql_stmt_bind_param(stmt, &param);
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return ok;
}

/* 获取水印配置 */
bool FileMetaDAO::getWatermark(const std::string& file_id, std::string& text, 
                               std::string& position, int& opacity) {
    MYSQL* conn = getConn();
    if (!conn) return false;
    
    const char* sql = "SELECT watermark_text, watermark_position, watermark_opacity, watermark_text IS NOT NULL AS has_wm FROM files WHERE file_id=?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt || mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        if (stmt) mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }
    
    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    std::string fid = file_id;
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = &fid[0];
    param.buffer_length = fid.size();
    mysql_stmt_bind_param(stmt, &param);
    
    MYSQL_BIND result[4];
    memset(result, 0, sizeof(result));
    char text_buf[256] = {0};
    char pos_buf[32] = {0};
    int op_val = 0;
    int has_wm_val = 0;
    bool text_null = true;
    bool pos_null = true;
    bool op_null = true;
    bool has_wm_null = true;
    unsigned long text_len = 0;
    unsigned long pos_len = 0;
    
    result[0].buffer_type = MYSQL_TYPE_STRING;
    result[0].buffer = text_buf;
    result[0].buffer_length = sizeof(text_buf);
    result[0].is_null = &text_null;
    result[0].length = &text_len;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = pos_buf;
    result[1].buffer_length = sizeof(pos_buf);
    result[1].is_null = &pos_null;
    result[1].length = &pos_len;
    result[2].buffer_type = MYSQL_TYPE_LONG;
    result[2].buffer = &op_val;
    result[2].is_null = &op_null;
    result[3].buffer_type = MYSQL_TYPE_LONG;
    result[3].buffer = &has_wm_val;
    result[3].is_null = &has_wm_null;
    
    mysql_stmt_bind_result(stmt, result);
    
    bool found = false;
    if (mysql_stmt_execute(stmt) == 0 && mysql_stmt_fetch(stmt) == 0) {
        AERO_LOG_INFO("[getWatermark] has_wm_val=" + std::to_string(has_wm_val) + " text_null=" + std::to_string(text_null) + " text_buf_len=" + std::to_string(strlen(text_buf)));
        if (has_wm_val && !text_null && text_buf[0] != '\0') {
            text = text_buf;
            position = pos_null ? "" : pos_buf;
            opacity = op_null ? 50 : op_val;
            found = true;
        }
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return found;
}
