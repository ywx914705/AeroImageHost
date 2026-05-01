#include <curl/curl.h>
#include "Handlers.hpp"
#include "Auth.hpp"
#include "FileMeta.hpp"
#include "MinIOClient.hpp"
#include "ImageProcessor.hpp"
#include "ConnectionPool.hpp"
#include "RedisClient.hpp"
#include "Utils.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include "EmailVerificationDAO.hpp"
#include "EmailSender.hpp"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <cpprest/json.h>

using namespace rapidjson;

static void releaseConn(MYSQL* conn) {
    ConnectionPool::getInstance().releaseConnection(conn);
}

web::json::value docToJsonValue(const Document& doc) {
    std::string json = serializeJson(doc);
    try {
        return web::json::value::parse(json);
    } catch (const std::exception& e) {
        LOG_ERROR("docToJsonValue parse error: " + std::string(e.what()) + ", json: " + json.substr(0, 200));
        web::json::value err;
        err["error"] = web::json::value::string("JSON parse error");
        return err;
    }
}

// 固定盐值（兼容现有 users 表结构，无 password_salt 字段）
static const std::string PASSWORD_SALT = "aero_image_host_2026_secure_salt";

static std::string sha256(const std::string& str) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    // 使用 OpenSSL 3.0 兼容的 EVP API
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, str.c_str(), str.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &len) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for (unsigned int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

static bool uploadFileToMinIO(const std::string& file_id, const std::vector<char>& file_data, const std::string& content_type) {
    auto& client = MinIOClient::instance();
    bool ok = client.putObject(file_id, file_data, content_type);
    if (!ok) {
        LOG_ERROR("MinIO SDK upload failed: " + file_id);
    }
    return ok;
}

web::json::value handleUpload(int user_id, const std::string& filename, const std::vector<char>& file_data, const std::string& content_type) {
    size_t maxSize = static_cast<size_t>(Config::instance().getInt("max_file_size", 100 * 1024 * 1024));
    if (file_data.size() > maxSize) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File too large", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string ext = getFileExtension(filename);
    std::vector<std::string> allowed = {"jpg","jpeg","png","gif","webp","pdf","doc","docx","xls","xlsx","ppt","pptx","txt","zip","rar","7z"};
    if (!isAllowedExtension(ext, allowed)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File type not allowed", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string file_id = generateUUID();

    // 智能检测 MIME 类型：优先使用客户端提供的，否则根据扩展名推断
    std::string detected_mime;
    if (!content_type.empty() && isValidImageMimeType(content_type)) {
        detected_mime = content_type;
    } else {
        detected_mime = getMimeTypeFromExtension(filename);
    }

    FileMeta meta;
    meta.file_id = file_id;
    meta.user_id = user_id;
    meta.filename = filename;
    meta.size = file_data.size();
    meta.mime_type = detected_mime;
    meta.width = 0;
    meta.height = 0;
    meta.upload_time = time(nullptr);
    meta.is_public = false;

    if (!FileMetaDAO::instance().save(meta)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to save metadata", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    if (!uploadFileToMinIO(file_id, file_data, detected_mime)) {
        FileMetaDAO::instance().del(file_id);
        Document resp; resp.SetObject();
        resp.AddMember("error", "MinIO upload failed", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600);
    Document resp; resp.SetObject();
    resp.AddMember("file_id", StringRef(file_id.c_str()), resp.GetAllocator());
    resp.AddMember("presign_url", StringRef(presignUrl.c_str()), resp.GetAllocator());
    resp.AddMember("filename", StringRef(filename.c_str()), resp.GetAllocator());
    resp.AddMember("size", static_cast<uint64_t>(file_data.size()), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(detected_mime.c_str()), resp.GetAllocator());
    resp.AddMember("download_url", StringRef((Config::instance().getString("minio.public_url") + file_id).c_str()), resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleListFiles(int user_id, int offset, int limit, const std::string& search_keyword) {
    auto files = FileMetaDAO::instance().listByUserWithSearch(user_id, search_keyword, offset, limit);
    int total = FileMetaDAO::instance().countByUserWithSearch(user_id, search_keyword);
    LOG_INFO("handleListFiles DB result: user_id=" + std::to_string(user_id) + " offset=" + std::to_string(offset) + " limit=" + std::to_string(limit) + " search='" + search_keyword + "' files_returned=" + std::to_string(files.size()) + " total=" + std::to_string(total));

    Document resp; resp.SetObject();
    Value filesArr(kArrayType);
    for (const auto& f : files) {
        Value obj(kObjectType);
        obj.AddMember("file_id", StringRef(f.file_id.c_str()), resp.GetAllocator());
        obj.AddMember("filename", StringRef(f.filename.c_str()), resp.GetAllocator());
        obj.AddMember("size", static_cast<uint64_t>(f.size), resp.GetAllocator());
        obj.AddMember("mime_type", StringRef(f.mime_type.c_str()), resp.GetAllocator());
        obj.AddMember("width", f.width, resp.GetAllocator());
        obj.AddMember("height", f.height, resp.GetAllocator());
        obj.AddMember("upload_time", static_cast<int64_t>(f.upload_time), resp.GetAllocator());
        obj.AddMember("is_public", (f.is_public != 0), resp.GetAllocator());
        obj.AddMember("view_count", static_cast<uint64_t>(f.view_count), resp.GetAllocator());
        std::string pubUrl = Config::instance().getString("minio.public_url") + f.file_id;
        obj.AddMember("download_url", StringRef(pubUrl.c_str()), resp.GetAllocator());
        filesArr.PushBack(obj, resp.GetAllocator());
    }
    resp.AddMember("files", filesArr, resp.GetAllocator());
    // 使用数据库中的总数，确保分页正常工作
    resp.AddMember("total", total, resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleDeleteFile(int user_id, const std::string& file_id) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File not found", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    if (meta.user_id != user_id) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Permission denied", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    if (!MinIOClient::instance().deleteObject(file_id)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to delete file from storage", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    if (!FileMetaDAO::instance().del(file_id)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to delete metadata", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleBatchDeleteFiles(int user_id, const std::vector<std::string>& file_ids) {
    Document resp; resp.SetObject();
    int success_count = 0;
    int fail_count = 0;

    for (const auto& file_id : file_ids) {
        FileMeta meta = FileMetaDAO::instance().get(file_id);
        if (meta.file_id.empty()) {
            fail_count++;
            continue;
        }
        if (meta.user_id != user_id) {
            fail_count++;
            continue;
        }
        if (!MinIOClient::instance().deleteObject(file_id)) {
            fail_count++;
            continue;
        }
        if (!FileMetaDAO::instance().del(file_id)) {
            fail_count++;
            continue;
        }
        success_count++;
    }

    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("deleted_count", success_count, resp.GetAllocator());
    resp.AddMember("failed_count", fail_count, resp.GetAllocator());
    return docToJsonValue(resp);
}

std::pair<std::vector<char>, std::string> handleGetFile(const std::string& file_id, bool check_auth, int user_id, const std::string& user_agent) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) return { {}, "" };
    if (check_auth && !meta.is_public && meta.user_id != user_id) return { {}, "" };

    std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600);
    return { {}, presignUrl };
}

web::json::value handleShare(const std::string& file_id) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File not found", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    std::string url = Config::instance().getString("minio.public_url") + file_id;
    Document resp; resp.SetObject();
    resp.AddMember("share_url", StringRef(url.c_str()), resp.GetAllocator());
    resp.AddMember("filename", StringRef(meta.filename.c_str()), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(meta.mime_type.c_str()), resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleSetPublic(int user_id, const std::string& file_id) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File not found", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    if (meta.user_id != user_id) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Permission denied", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    bool new_public = !meta.is_public;
    if (!FileMetaDAO::instance().updatePublic(file_id, new_public)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to update metadata", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("is_public", new_public, resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleRegister(const std::string& account, const std::string& password) {
    if (account.empty() || password.empty()) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Account and password are required", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    if (account.length() < 3 || account.length() > 64) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Account must be 3-64 characters", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    if (password.length() < 6) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Password must be at least 6 characters", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    MYSQL* conn = ConnectionPool::getInstance().getConnection();
    if (!conn) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Database error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    const char* sql = "INSERT INTO users (account, password_hash) VALUES (?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 使用固定盐值 + SHA256 哈希（兼容现有表结构）
    std::string salted_password = PASSWORD_SALT + password;
    std::string hash = sha256(salted_password);

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)account.c_str();
    bind[0].buffer_length = account.size();
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)hash.c_str();
    bind[1].buffer_length = hash.size();
    mysql_stmt_bind_param(stmt, bind);

    int ret = mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    releaseConn(conn);

    if (ret != 0) {
        // 检查是否是唯一键冲突（账号已存在）
        std::string errorMsg = "Account exists";
        Document resp; resp.SetObject();
        resp.AddMember("error", StringRef(errorMsg.c_str()), resp.GetAllocator());
        return docToJsonValue(resp);
    }

    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleLogin(const std::string& account, const std::string& password) {
    if (account.empty() || password.empty()) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Account and password are required", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    MYSQL* conn = ConnectionPool::getInstance().getConnection();
    if (!conn) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Database error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    const char* sql = "SELECT id, password_hash FROM users WHERE account = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
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
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    int user_id = 0;
    char storedHash[65] = {0};
    MYSQL_BIND result[2];
    memset(result, 0, sizeof(result));
    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = &user_id;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = storedHash;
    result[1].buffer_length = sizeof(storedHash) - 1;
    mysql_stmt_bind_result(stmt, result);
    mysql_stmt_store_result(stmt);

    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "Invalid credentials", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);

    // 使用固定盐值验证密码
    std::string computed = sha256(PASSWORD_SALT + password);
    if (computed != std::string(storedHash)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Invalid credentials", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string token = Auth::generateToken(user_id, "");
    if (token.empty()) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to generate authentication token", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    Document resp; resp.SetObject();
    resp.AddMember("token", StringRef(token.c_str()), resp.GetAllocator());
    resp.AddMember("user_id", user_id, resp.GetAllocator());
    resp.AddMember("account", StringRef(account.c_str()), resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleRequestUploadUrl(int user_id, const std::string& filename,
                                         const std::string& content_type, size_t file_size) {
    size_t maxSize = static_cast<size_t>(Config::instance().getInt("max_file_size", 100 * 1024 * 1024));
    if (file_size > maxSize) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File too large", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string ext = getFileExtension(filename);
    std::vector<std::string> allowed = {"jpg","jpeg","png","gif","webp","pdf","doc","docx","xls","xlsx","ppt","pptx","txt","zip","rar","7z"};
    if (!isAllowedExtension(ext, allowed)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File type not allowed", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string file_id = generateUUID();
    std::string detectedMime = getMimeTypeFromExtension(filename);

    // 生成预签名 PUT URL
    int expires = 7200; // 2小时有效期
    std::string presignUrl = MinIOClient::instance().presignPutUrl(file_id, expires);

    Document resp; resp.SetObject();
    resp.AddMember("file_id", StringRef(file_id.c_str()), resp.GetAllocator());
    resp.AddMember("upload_url", StringRef(presignUrl.c_str()), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(detectedMime.c_str()), resp.GetAllocator());
    resp.AddMember("expires_in", expires, resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleConfirmUpload(int user_id, const std::string& file_id,
                                      const std::string& filename,
                                      const std::string& content_type,
                                      size_t file_size) {
    if (!MinIOClient::instance().objectExists(file_id)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File not found in storage", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string mime_type = content_type.empty() ? getMimeTypeFromExtension(filename) : content_type;

    FileMeta meta;
    meta.file_id = file_id;
    meta.user_id = user_id;
    meta.filename = filename;
    meta.size = file_size;
    meta.mime_type = mime_type;
    meta.width = 0;
    meta.height = 0;
    meta.upload_time = time(nullptr);
    meta.is_public = false;

    if (!FileMetaDAO::instance().save(meta)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to save metadata", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600);
    Document resp; resp.SetObject();
    resp.AddMember("file_id", StringRef(file_id.c_str()), resp.GetAllocator());
    resp.AddMember("presign_url", StringRef(presignUrl.c_str()), resp.GetAllocator());
    resp.AddMember("filename", StringRef(filename.c_str()), resp.GetAllocator());
    resp.AddMember("size", static_cast<uint64_t>(file_size), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(mime_type.c_str()), resp.GetAllocator());
    resp.AddMember("download_url", StringRef(
        (Config::instance().getString("minio.public_url") + file_id).c_str()), resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleStats() {
    MYSQL* conn = ConnectionPool::getInstance().getConnection();
    if (!conn) {
        LOG_ERROR("handleStats: failed to get database connection");
        Document resp; resp.SetObject();
        resp.AddMember("error", "Database error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    int total_users = 0, total_files = 0, total_images = 0;
    struct { const char* sql; int* out; } queries[] = {
        {"SELECT COUNT(*) FROM users", &total_users},
        {"SELECT COUNT(*) FROM files", &total_files},
        {"SELECT COUNT(*) FROM files WHERE mime_type LIKE 'image/%'", &total_images},
    };

    for (auto& q : queries) {
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (!stmt) continue;
        if (mysql_stmt_prepare(stmt, q.sql, strlen(q.sql)) != 0) {
            LOG_ERROR(std::string("handleStats prepare failed: ") + mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            continue;
        }
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR(std::string("handleStats execute failed: ") + mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            continue;
        }
        MYSQL_BIND result; memset(&result, 0, sizeof(result));
        result.buffer_type = MYSQL_TYPE_LONG;
        result.buffer = q.out;
        mysql_stmt_bind_result(stmt, &result);
        mysql_stmt_store_result(stmt);
        if (mysql_stmt_fetch(stmt) != 0) {
            LOG_ERROR(std::string("handleStats fetch failed: ") + mysql_stmt_error(stmt));
        }
        mysql_stmt_close(stmt);
    }
    ConnectionPool::getInstance().releaseConnection(conn);

    LOG_INFO("handleStats: users=" + std::to_string(total_users) +
             " files=" + std::to_string(total_files) +
             " images=" + std::to_string(total_images));

    Document resp; resp.SetObject();
    resp.AddMember("total_users", total_users, resp.GetAllocator());
    resp.AddMember("total_files", total_files, resp.GetAllocator());
    resp.AddMember("total_images", total_images, resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleSendVerificationCode(const std::string& email) {
    if (email.empty()) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Email is required", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 简单的邮箱格式验证
    if (email.find("@") == std::string::npos || email.find(".") == std::string::npos) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Invalid email format", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 生成6位数字验证码
    std::string code = EmailSender::generateVerificationCode(6);

    // 保存验证码到数据库
    if (!EmailVerificationDAO::instance().save(email, code)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to save verification code", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 发送验证码邮件
    if (!EmailSender::instance().sendVerificationEmail(email, code)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to send verification email", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("message", "Verification code sent successfully", resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleEmailRegister(const std::string& account, const std::string& password, const std::string& email, const std::string& code) {
    if (account.empty() || password.empty() || email.empty() || code.empty()) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "All fields are required", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 验证账号长度
    if (account.length() < 3 || account.length() > 64) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Account must be 3-64 characters", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 验证密码长度
    if (password.length() < 6) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Password must be at least 6 characters", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 验证邮箱格式
    if (email.find("@") == std::string::npos || email.find(".") == std::string::npos) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Invalid email format", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 验证验证码
    if (!EmailVerificationDAO::instance().verifyCode(email, code)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Invalid or expired verification code", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 检查验证码是否已被使用
    if (EmailVerificationDAO::instance().isCodeUsed(email, code)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Verification code already used", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    MYSQL* conn = ConnectionPool::getInstance().getConnection();
    if (!conn) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Database error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 开始事务
    mysql_autocommit(conn, 0);

    const char* sql_check_email = "SELECT id FROM users WHERE email = ?";
    MYSQL_STMT* stmt_check_email = mysql_stmt_init(conn);
    if (!stmt_check_email) {
        mysql_rollback(conn);
        mysql_autocommit(conn, 1);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    if (mysql_stmt_prepare(stmt_check_email, sql_check_email, strlen(sql_check_email)) != 0) {
        mysql_stmt_close(stmt_check_email);
        mysql_rollback(conn);
        mysql_autocommit(conn, 1);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    MYSQL_BIND bind_check[1];
    memset(bind_check, 0, sizeof(bind_check));
    bind_check[0].buffer_type = MYSQL_TYPE_STRING;
    bind_check[0].buffer = (char*)email.c_str();
    bind_check[0].buffer_length = email.size();

    if (mysql_stmt_bind_param(stmt_check_email, bind_check) != 0) {
        mysql_stmt_close(stmt_check_email);
        mysql_rollback(conn);
        mysql_autocommit(conn, 1);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    if (mysql_stmt_execute(stmt_check_email) != 0) {
        mysql_stmt_close(stmt_check_email);
        mysql_rollback(conn);
        mysql_autocommit(conn, 1);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    MYSQL_BIND result_bind;
    memset(&result_bind, 0, sizeof(result_bind));
    int user_id;
    result_bind.buffer_type = MYSQL_TYPE_LONG;
    result_bind.buffer = &user_id;

    if (mysql_stmt_bind_result(stmt_check_email, &result_bind) != 0) {
        mysql_stmt_close(stmt_check_email);
        mysql_rollback(conn);
        mysql_autocommit(conn, 1);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    bool email_exists = false;
    if (mysql_stmt_fetch(stmt_check_email) == 0) {
        email_exists = true;
    }

    mysql_stmt_close(stmt_check_email);

    if (email_exists) {
        mysql_rollback(conn);
        mysql_autocommit(conn, 1);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "Email already registered", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    const char* sql_insert = "INSERT INTO users (account, password_hash, email) VALUES (?, ?, ?)";
    MYSQL_STMT* stmt_insert = mysql_stmt_init(conn);
    if (!stmt_insert) {
        mysql_rollback(conn);
        mysql_autocommit(conn, 1);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    if (mysql_stmt_prepare(stmt_insert, sql_insert, strlen(sql_insert)) != 0) {
        mysql_stmt_close(stmt_insert);
        mysql_rollback(conn);
        mysql_autocommit(conn, 1);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 使用固定盐值 + SHA256 哈希
    std::string salted_password = PASSWORD_SALT + password;
    std::string hash = sha256(salted_password);

    MYSQL_BIND bind_insert[3];
    memset(bind_insert, 0, sizeof(bind_insert));
    bind_insert[0].buffer_type = MYSQL_TYPE_STRING;
    bind_insert[0].buffer = (char*)account.c_str();
    bind_insert[0].buffer_length = account.size();
    bind_insert[1].buffer_type = MYSQL_TYPE_STRING;
    bind_insert[1].buffer = (char*)hash.c_str();
    bind_insert[1].buffer_length = hash.size();
    bind_insert[2].buffer_type = MYSQL_TYPE_STRING;
    bind_insert[2].buffer = (char*)email.c_str();
    bind_insert[2].buffer_length = email.size();

    if (mysql_stmt_bind_param(stmt_insert, bind_insert) != 0) {
        mysql_stmt_close(stmt_insert);
        mysql_rollback(conn);
        mysql_autocommit(conn, 1);
        releaseConn(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "DB error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    int ret = mysql_stmt_execute(stmt_insert);
    mysql_stmt_close(stmt_insert);

    if (ret != 0) {
        // 检查是否是唯一键冲突
        std::string errorMsg = mysql_error(conn);
        if (errorMsg.find("Duplicate entry") != std::string::npos) {
            mysql_rollback(conn);
            mysql_autocommit(conn, 1);
            releaseConn(conn);
            Document resp; resp.SetObject();
            resp.AddMember("error", "Account or email already exists", resp.GetAllocator());
            return docToJsonValue(resp);
        } else {
            mysql_rollback(conn);
            mysql_autocommit(conn, 1);
            releaseConn(conn);
            Document resp; resp.SetObject();
            resp.AddMember("error", "Registration failed", resp.GetAllocator());
            return docToJsonValue(resp);
        }
    }

    // 提交事务
    mysql_commit(conn);
    mysql_autocommit(conn, 1);
    releaseConn(conn);

    // 标记验证码为已使用
    EmailVerificationDAO::instance().markCodeAsUsed(email, code);

    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("message", "Registration successful", resp.GetAllocator());
    return docToJsonValue(resp);
}
