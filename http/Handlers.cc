/*
 * Handlers 模块 - 业务逻辑处理实现
 *
 * 职责：实现所有 API 端点的业务逻辑，包括：
 *   - 用户注册/登录（SHA-256 密码哈希 + Redis Token）
 *   - 文件上传（直接上传 + 预签名上传 + 分片上传三种模式）
 *   - 文件管理（列表、搜索、删除、批量删除、公开/私有切换）
 *   - 缩略图生成（libvips + MinIO 缓存）
 *   - 邮箱验证码（SMTP 异步发送）
 *   - 系统统计和孤儿文件清理
 *
 * 核心设计：
 *   - 无状态函数，所有参数由 HttpServer 传入
 *   - 使用 std::async 并行化 MinIO 操作（上传/批量删除）
 *   - 使用 AeroQueue 异步执行耗时操作（缩略图缓存、邮件发送）
 *   - 所有数据库操作通过各自的 DAO 类访问
 */
#include <curl/curl.h>
#include "Handlers.hpp"
#include "Auth.hpp"
#include "FileMeta.hpp"
#include "MinIOClient.hpp"
#include "ImageProcessor.hpp"
#include "UsersDAO.hpp"
#include "ConnectionPool.hpp"
#include "RedisClient.hpp"
#include "Utils.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include "EmailVerificationDAO.hpp"
#include "EmailSender.hpp"
#include "RateLimiter.hpp"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <chrono>
#include <rapidjson/stringbuffer.h>
#include <openssl/evp.h>
#include <sstream>

// 孤儿分片跟踪函数前向声明
static void trackMultipartUpload(const std::string& upload_id, int total_chunks);
static void untrackMultipartUpload(const std::string& upload_id);
#include <iomanip>
#include <cpprest/json.h>
#include <future>
#include <utility>
#include "AeroQueue.hpp"

using namespace rapidjson;

// 固定盐值（兼容现有 users 表中的密码哈希）
static const std::string PASSWORD_SALT = "aero_image_host_2026_secure_salt";

// SHA-256 哈希：用于密码存储
static std::string sha256(const std::string& str) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
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

web::json::value docToJsonValue(const Document& doc) {
    std::string json = serializeJson(doc);
    try {
        return web::json::value::parse(json);
    } catch (const std::exception& e) {
        LOG_ERROR("docToJsonValue parse error: " + std::string(e.what()) + ", json_len=" + std::to_string(json.size()));
        if (json.size() > 500) {
            LOG_ERROR("docToJsonValue json head: " + json.substr(0, 500));
            LOG_ERROR("docToJsonValue json tail: " + json.substr(json.size() - 200));
        } else {
            LOG_ERROR("docToJsonValue json: " + json);
        }
        web::json::value err;
        err["error"] = web::json::value::string("JSON parse error");
        return err;
    }
}

static bool uploadFileToMinIO(const std::string& file_id, const std::vector<unsigned char>& file_data, const std::string& content_type) {
    auto& client = MinIOClient::instance();
    bool ok = client.putObject(file_id, file_data, content_type);
    if (!ok) {
        LOG_ERROR("MinIO SDK upload失败: " + file_id);
    }
    return ok;
}

// 零拷贝上传：直接从 HTTP 层接收 unsigned char 向量，不经中间拷贝
web::json::value handleUpload(int user_id, const std::string& filename, const std::vector<unsigned char>& file_data, const std::string& content_type) {
    auto uploadStart = std::chrono::steady_clock::now();
    size_t maxSize = static_cast<size_t>(Config::instance().getInt("max_file_size", 100 * 1024 * 1024));
    if (file_data.size() > maxSize) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File too large", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string ext = getFileExtension(filename);
    std::vector<std::string> allowed = {"jpg","jpeg","png","gif","webp","pdf","doc","docx","xls","xlsx","ppt","pptx","txt","zip","rar","7z","mp4","mov","avi","mkv","webm","mp3","wav","flac","ogg"};
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

    // 顺序执行：先 MinIO 后 MySQL，确保数据一致性
    // 如果 MinIO 失败，无需清理；如果 MySQL 失败，回滚 MinIO
    auto uploadStart2 = std::chrono::steady_clock::now();
    bool minioOk = uploadFileToMinIO(file_id, file_data, detected_mime);
    if (!minioOk) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "MinIO upload failed", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    bool mysqlOk = FileMetaDAO::instance().save(meta);
    if (!mysqlOk) {
        // 回滚：删除已上传的 MinIO 对象
        MinIOClient::instance().deleteObject(file_id);
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to save metadata", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    auto afterMinio = std::chrono::steady_clock::now();

    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterMinio - uploadStart).count();
    LOG_INFO("[Upload] " + filename + " (" + std::to_string(file_data.size()) + " bytes) - total:" +
             std::to_string(totalMs) + "ms (parallel mysql+minio)");

    std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600);
    std::string downloadUrl = Config::instance().getString("minio.public_url") + file_id;
    Document resp; resp.SetObject();
    resp.AddMember("file_id", StringRef(file_id.c_str()), resp.GetAllocator());
    resp.AddMember("presign_url", StringRef(presignUrl.c_str()), resp.GetAllocator());
    resp.AddMember("filename", StringRef(filename.c_str()), resp.GetAllocator());
    resp.AddMember("size", static_cast<uint64_t>(file_data.size()), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(detected_mime.c_str()), resp.GetAllocator());
    resp.AddMember("download_url", StringRef(downloadUrl.c_str()), resp.GetAllocator());
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
        // 使用 Value 拷贝构造，避免 StringRef 悬空指针
        {
            std::string pubUrl = Config::instance().getString("minio.public_url") + f.file_id;
            Value downloadUrlVal;
            downloadUrlVal.SetString(pubUrl.c_str(), pubUrl.size(), resp.GetAllocator());
            obj.AddMember("download_url", downloadUrlVal, resp.GetAllocator());
        }
        // 标记需要预览的文件类型（前端按需请求 presign_url）
        std::string mime = f.mime_type;
        bool needsPreview = (mime.find("pdf") != std::string::npos ||
                             mime.find("video") != std::string::npos ||
                             mime.find("audio") != std::string::npos);
        obj.AddMember("needs_preview", needsPreview, resp.GetAllocator());
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
    // 先删 MySQL 元数据，再删 MinIO 对象
    // 如果 MinIO 删除失败，孤儿清理任务会兜底
    if (!FileMetaDAO::instance().del(file_id)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to delete metadata", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    MinIOClient::instance().deleteObject(file_id);
    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleBatchDeleteFiles(int user_id, const std::vector<std::string>& file_ids) {
    Document resp; resp.SetObject();

    if (file_ids.empty()) {
        resp.AddMember("status", "success", resp.GetAllocator());
        resp.AddMember("deleted_count", 0, resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 1. 批量查询验证归属权
    auto valid_ids = FileMetaDAO::instance().getValidFileIds(file_ids, user_id);

    // 2. 并行删除 MinIO 对象
    std::vector<std::future<bool>> deleteFutures;
    deleteFutures.reserve(valid_ids.size());
    for (const auto& fid : valid_ids) {
        std::string key = fid;
        deleteFutures.push_back(std::async(std::launch::async, [key]() {
            return MinIOClient::instance().deleteObject(key);
        }));
    }
    for (auto& f : deleteFutures) {
        f.get();
    }

    // 3. 批量删除 MySQL 记录
    if (!valid_ids.empty()) {
        FileMetaDAO::instance().deleteFilesBatch(valid_ids);
    }

    int success_count = static_cast<int>(valid_ids.size());
    int fail_count = static_cast<int>(file_ids.size()) - success_count;

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

    std::string hash = sha256(PASSWORD_SALT + password);
    bool ok = UsersDAO::instance().registerUser(account, hash);

    if (!ok) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Account exists", resp.GetAllocator());
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

    // 登录频率限制
    const int MAX_LOGIN_ATTEMPTS = Config::instance().getInt("security.max_login_attempts", 5);
    const int LOGIN_WINDOW_SECONDS = Config::instance().getInt("security.login_window_seconds", 900);
    std::string rateLimitKey = "login:" + account;

    if (!RateLimiter::isAllowed(rateLimitKey, MAX_LOGIN_ATTEMPTS, LOGIN_WINDOW_SECONDS)) {
        LOG_WARN("[Login] Rate limit exceeded for account: " + account);
        Document resp; resp.SetObject();
        resp.AddMember("error", "Too many failed attempts. Please try again later.", resp.GetAllocator());
        resp.AddMember("retry_after_seconds", LOGIN_WINDOW_SECONDS, resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 查询用户
    std::string storedHash;
    int user_id = UsersDAO::instance().loginUser(account, storedHash);

    if (user_id == 0) {
        RateLimiter::recordFailure(rateLimitKey, LOGIN_WINDOW_SECONDS);
        Document resp; resp.SetObject();
        resp.AddMember("error", "Invalid credentials", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    if (user_id < 0) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Database error", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 验证密码
    std::string computed = sha256(PASSWORD_SALT + password);
    if (computed != storedHash) {
        RateLimiter::recordFailure(rateLimitKey, LOGIN_WINDOW_SECONDS);
        int remaining = RateLimiter::getRemainingAttempts(rateLimitKey, MAX_LOGIN_ATTEMPTS, LOGIN_WINDOW_SECONDS);
        LOG_WARN("[Login] 登录失败: " + account + ", 剩余次数: " + std::to_string(remaining));
        Document resp; resp.SetObject();
        resp.AddMember("error", "Invalid credentials", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 登录成功，重置频率限制
    RateLimiter::reset(rateLimitKey);

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
    std::vector<std::string> allowed = {"jpg","jpeg","png","gif","webp","pdf","doc","docx","xls","xlsx","ppt","pptx","txt","zip","rar","7z","mp4","mov","avi","mkv","webm","mp3","wav","flac","ogg"};
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
    std::string downloadUrl = Config::instance().getString("minio.public_url") + file_id;
    Document resp; resp.SetObject();
    resp.AddMember("file_id", StringRef(file_id.c_str()), resp.GetAllocator());
    resp.AddMember("presign_url", StringRef(presignUrl.c_str()), resp.GetAllocator());
    resp.AddMember("filename", StringRef(filename.c_str()), resp.GetAllocator());
    resp.AddMember("size", static_cast<uint64_t>(file_size), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(mime_type.c_str()), resp.GetAllocator());
    resp.AddMember("download_url", StringRef(downloadUrl.c_str()), resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleStats() {
    int total_users = 0, total_files = 0, total_images = 0;
    long long total_size = 0;

    FileMetaDAO::instance().getFileStats(total_files, total_images, total_size);
    total_users = UsersDAO::instance().getUserCount();

    LOG_INFO("handleStats: users=" + std::to_string(total_users) +
             " files=" + std::to_string(total_files) +
             " images=" + std::to_string(total_images) +
             " size=" + std::to_string(total_size));

    Document resp; resp.SetObject();
    resp.AddMember("total_users", total_users, resp.GetAllocator());
    resp.AddMember("total_files", total_files, resp.GetAllocator());
    resp.AddMember("total_images", total_images, resp.GetAllocator());
    resp.AddMember("total_size", static_cast<int64_t>(total_size), resp.GetAllocator());
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

    // 异步发送验证码邮件（不阻塞请求）
    std::string emailCopy = email;
    std::string codeCopy = code;
    AeroQueue::instance().post([emailCopy, codeCopy]() {
        if (!EmailSender::instance().sendVerificationEmail(emailCopy, codeCopy)) {
            LOG_ERROR("[Email] Failed to send verification code to: " + emailCopy);
        }
    });

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

    // 检查邮箱是否已注册
    if (UsersDAO::instance().emailExists(conn, email)) {
        mysql_rollback(conn);
        mysql_autocommit(conn, 1);
        ConnectionPool::getInstance().releaseConnection(conn);
        Document resp; resp.SetObject();
        resp.AddMember("error", "Email already registered", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 插入新用户
    std::string hash = sha256(PASSWORD_SALT + password);
    int ret = UsersDAO::instance().registerUserWithEmail(conn, account, hash, email);

    if (ret != 0) {
        mysql_rollback(conn);
        mysql_autocommit(conn, 1);
        ConnectionPool::getInstance().releaseConnection(conn);
        Document resp; resp.SetObject();
        if (ret == -1) {
            resp.AddMember("error", "Account or email already exists", resp.GetAllocator());
        } else {
            resp.AddMember("error", "Registration failed", resp.GetAllocator());
        }
        return docToJsonValue(resp);
    }

    // 提交事务
    mysql_commit(conn);
    mysql_autocommit(conn, 1);
    ConnectionPool::getInstance().releaseConnection(conn);

    // 标记验证码为已使用
    EmailVerificationDAO::instance().markCodeAsUsed(email, code);

    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("message", "Registration successful", resp.GetAllocator());
    return docToJsonValue(resp);
}

// 分片上传接口

web::json::value handleMultipartInit(int user_id, const std::string& filename,
                                     const std::string& content_type, size_t file_size) {
    size_t maxSize = static_cast<size_t>(Config::instance().getInt("max_file_size", 100 * 1024 * 1024));
    if (file_size > maxSize) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File too large", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string ext = getFileExtension(filename);
    std::vector<std::string> allowed = {"jpg","jpeg","png","gif","webp","pdf","doc","docx","xls","xlsx","ppt","pptx","txt","zip","rar","7z","mp4","mov","avi","mkv","webm","mp3","wav","flac","ogg"};
    if (!isAllowedExtension(ext, allowed)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File type not allowed", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string upload_id = generateUUID();
    size_t chunk_size = 20 * 1024 * 1024; // 20MB per chunk, reduce request count
    int total_chunks = static_cast<int>((file_size + chunk_size - 1) / chunk_size);

    // Track upload in Redis for orphan cleanup
    RedisClient::instance().set("upload_active:" + upload_id, std::to_string(total_chunks));
    RedisClient::instance().expire("upload_active:" + upload_id, 86400);
    trackMultipartUpload(upload_id, total_chunks);

    Document resp; resp.SetObject();
    resp.AddMember("upload_id", StringRef(upload_id.c_str()), resp.GetAllocator());
    resp.AddMember("chunk_size", static_cast<uint64_t>(chunk_size), resp.GetAllocator());
    resp.AddMember("total_chunks", total_chunks, resp.GetAllocator());

    Value chunksArr(kArrayType);
    for (int i = 0; i < total_chunks; ++i) {
        Value chunkObj(kObjectType);
        chunkObj.AddMember("part_number", i, resp.GetAllocator());
        chunksArr.PushBack(chunkObj, resp.GetAllocator());
    }
    resp.AddMember("chunks", chunksArr, resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleMultipartUploadChunk(const std::string& upload_id, int part_number,
                                            const std::vector<unsigned char>& data) {
    if (upload_id.empty() || data.empty()) {
        LOG_ERROR("[Chunk] empty upload_id or data: id=" + upload_id + " part=" + std::to_string(part_number) + " size=" + std::to_string(data.size()));
        Document resp; resp.SetObject();
        resp.AddMember("error", "Invalid upload_id or empty chunk", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string chunk_key = "chunks/" + upload_id + "/" + std::to_string(part_number);
    if (!MinIOClient::instance().putObject(chunk_key, data, "application/octet-stream")) {
        LOG_ERROR("[Chunk] MinIO putObject failed: " + chunk_key + " size=" + std::to_string(data.size()));
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to upload chunk to storage", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    LOG_INFO("[Chunk] success: " + chunk_key + " (" + std::to_string(data.size()) + " bytes)");
    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("part_number", part_number, resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleMultipartComplete(int user_id, const std::string& upload_id,
                                         const std::string& filename, const std::string& content_type,
                                         size_t file_size, int total_chunks) {
    std::vector<std::string> chunk_keys;
    for (int i = 0; i < total_chunks; ++i) {
        chunk_keys.push_back("chunks/" + upload_id + "/" + std::to_string(i));
    }

    if (chunk_keys.empty()) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "No chunks found for upload", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // 合并前验证所有分片是否都已上传，防止生成损坏文件
    std::vector<std::string> missingChunks;
    for (const auto& key : chunk_keys) {
        if (!MinIOClient::instance().objectExists(key)) {
            missingChunks.push_back(key);
        }
    }
    if (!missingChunks.empty()) {
        LOG_ERROR("[Multipart] Missing chunks for upload_id=" + upload_id +
                  ", missing=" + std::to_string(missingChunks.size()) +
                  "/" + std::to_string(total_chunks));
        // Cleanup: delete chunks that do exist
        for (const auto& key : chunk_keys) {
            MinIOClient::instance().deleteObject(key);
        }
        Document resp; resp.SetObject();
        resp.AddMember("error", "Incomplete upload: missing chunks", resp.GetAllocator());
        resp.AddMember("missing_count", static_cast<int>(missingChunks.size()), resp.GetAllocator());
        resp.AddMember("total_chunks", total_chunks, resp.GetAllocator());
        return docToJsonValue(resp);
    }

    std::string file_id = generateUUID();
    std::string detected_mime = content_type.empty() ? getMimeTypeFromExtension(filename) : content_type;

    // Compose chunks (synchronous — file must exist before responding)
    if (!MinIOClient::instance().composeObjects(file_id, detected_mime, chunk_keys)) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to compose chunks", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // Save metadata
    FileMeta meta;
    meta.file_id = file_id;
    meta.user_id = user_id;
    meta.filename = filename;
    meta.size = file_size;
    meta.mime_type = detected_mime;
    meta.width = 0;
    meta.height = 0;
    meta.upload_time = time(nullptr);
    meta.is_public = false;

    if (!FileMetaDAO::instance().save(meta)) {
        MinIOClient::instance().deleteObject(file_id);
        Document resp; resp.SetObject();
        resp.AddMember("error", "Failed to save metadata", resp.GetAllocator());
        return docToJsonValue(resp);
    }

    // Remove upload tracking from Redis
    RedisClient::instance().del("upload_active:" + upload_id);
    untrackMultipartUpload(upload_id);

    // Async parallel cleanup of temporary chunks (non-blocking)
    AeroQueue::instance().post([chunk_keys]() {
        std::vector<std::future<bool>> futures;
        futures.reserve(chunk_keys.size());
        for (const auto& key : chunk_keys) {
            std::string k = key;
            futures.push_back(std::async(std::launch::async, [k]() {
                return MinIOClient::instance().deleteObject(k);
            }));
        }
        for (auto& f : futures) f.get();
    });

    std::string presign_url = MinIOClient::instance().presignGetUrl(file_id, 3600);
    std::string pub_url = Config::instance().getString("minio.public_url") + file_id;

    Document resp; resp.SetObject();
    resp.AddMember("file_id", StringRef(file_id.c_str()), resp.GetAllocator());
    resp.AddMember("presign_url", StringRef(presign_url.c_str()), resp.GetAllocator());
    resp.AddMember("filename", StringRef(filename.c_str()), resp.GetAllocator());
    resp.AddMember("size", static_cast<uint64_t>(file_size), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(detected_mime.c_str()), resp.GetAllocator());
    resp.AddMember("download_url", StringRef(pub_url.c_str()), resp.GetAllocator());
    return docToJsonValue(resp);
}

web::json::value handleMultipartCleanup(int user_id, const std::string& upload_id) {
    for (int i = 0; i < 1000; ++i) {
        std::string chunk_key = "chunks/" + upload_id + "/" + std::to_string(i);
        if (!MinIOClient::instance().objectExists(chunk_key)) break;
        MinIOClient::instance().deleteObject(chunk_key);
    }
    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    return docToJsonValue(resp);
}

// 按需获取预签名 URL（避免文件列表时批量生成）
web::json::value handleGetPresignUrl(int user_id, const std::string& file_id) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "File not found", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    if (!meta.is_public && meta.user_id != user_id) {
        Document resp; resp.SetObject();
        resp.AddMember("error", "Permission denied", resp.GetAllocator());
        return docToJsonValue(resp);
    }
    std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600);
    Document resp; resp.SetObject();
    resp.AddMember("presign_url", StringRef(presignUrl.c_str()), resp.GetAllocator());
    return docToJsonValue(resp);
}

// 孤儿分片清理

// 将活跃的分片上传记录到 Redis 集合，用于超时后清理
static void trackMultipartUpload(const std::string& upload_id, int total_chunks) {
    std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    // Store as "upload_id:total_chunks:timestamp"
    RedisClient::instance().sadd("active_multipart_uploads",
        upload_id + ":" + std::to_string(total_chunks) + ":" + timestamp);
}

// 上传完成后从 Redis 集合中移除跟踪记录
static void untrackMultipartUpload(const std::string& upload_id) {
    auto members = RedisClient::instance().smembers("active_multipart_uploads");
    for (const auto& m : members) {
        if (m.find(upload_id + ":") == 0) {
            RedisClient::instance().srem("active_multipart_uploads", m);
            break;
        }
    }
}

// 扫描 Redis 中的活跃分片上传记录，删除超过 TTL 的孤儿分片
void cleanupOrphanChunks() {
    LOG_INFO("[Cleanup] 开始清理孤儿分片...");
    auto members = RedisClient::instance().smembers("active_multipart_uploads");
    if (members.empty()) {
        LOG_INFO("[Cleanup] 无活跃分片上传记录");
        return;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int MAX_AGE_SECONDS = Config::instance().getInt("cleanup.multipart_ttl_seconds", 86400);
    int cleaned = 0;

    for (const auto& member : members) {
        // 格式：upload_id:total_chunks:timestamp
        size_t p1 = member.find(':');
        size_t p2 = member.find(':', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) continue;

        std::string upload_id = member.substr(0, p1);
        int total_chunks = 0;
        long long timestamp = 0;
        try {
            total_chunks = std::stoi(member.substr(p1 + 1, p2 - p1 - 1));
            timestamp = std::stoll(member.substr(p2 + 1));
        } catch (...) { continue; }

        // Skip if still within TTL
        if ((now - timestamp) < MAX_AGE_SECONDS) continue;

        LOG_WARN("[Cleanup] Cleaning orphaned multipart upload: " + upload_id +
                 " (age=" + std::to_string(now - timestamp) + "s)");

        // Delete chunks in parallel
        std::vector<std::future<bool>> futures;
        futures.reserve(total_chunks);
        for (int i = 0; i < total_chunks; ++i) {
            std::string key = "chunks/" + upload_id + "/" + std::to_string(i);
            futures.push_back(std::async(std::launch::async, [key]() {
                return MinIOClient::instance().deleteObject(key);
            }));
        }
        for (auto& f : futures) f.get();

        // Remove from tracking set
        RedisClient::instance().srem("active_multipart_uploads", member);
        cleaned++;
    }

    LOG_INFO("[Cleanup] Orphan chunk cleanup completed, cleaned=" + std::to_string(cleaned) +
             " uploads, tracked=" + std::to_string(members.size()));
}
