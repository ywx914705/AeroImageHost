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
#include "MetricsCollector.hpp"
#include "WatermarkProcessor.hpp"
#include "EmailVerificationDAO.hpp"
#include "EmailSender.hpp"
#include "RateLimiter.hpp"
#include "PasswordHash.hpp"
#include "AeroQueue.hpp"
#include "TransactionGuard.hpp"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <openssl/evp.h>
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace rapidjson;

static const std::string PASSWORD_SALT = "aero_image_host_2026_secure_salt";

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

static bool uploadFileToMinIO(const std::string& file_id, const std::vector<unsigned char>& file_data, const std::string& content_type) {
    auto& client = MinIOClient::instance();
    bool ok = client.putObject(file_id, file_data, content_type);
    if (!ok) {
        LOG_ERROR("MinIO SDK upload failed: " + file_id);
    }
    return ok;
}

static void trackMultipartUpload(const std::string& upload_id, int total_chunks) {
    std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    RedisClient::instance().sadd("active_multipart_uploads",
        upload_id + ":" + std::to_string(total_chunks) + ":" + timestamp);
}

static void untrackMultipartUpload(const std::string& upload_id) {
    auto members = RedisClient::instance().smembers("active_multipart_uploads");
    for (const auto& m : members) {
        if (m.find(upload_id + ":") == 0) {
            RedisClient::instance().srem("active_multipart_uploads", m);
            break;
        }
    }
}

static const std::vector<std::string> ALLOWED_EXTENSIONS = {
    "jpg","jpeg","png","gif","webp","svg","pdf","doc","docx","xls","xlsx",
    "ppt","pptx","txt","zip","rar","7z","mp4","mov","avi","mkv","webm","mp3","wav","flac","ogg"
};

static void invalidateAggregateStatsCache();

HandlerResult handleRegister(const std::string& account, const std::string& password) {
    if (account.empty() || password.empty()) {
        return HandlerResult::error(errorResponse("Account and password are required"), 400);
    }
    if (account.length() < 3 || account.length() > 64) {
        return HandlerResult::error(errorResponse("Account must be 3-64 characters"), 400);
    }
    if (password.length() < 6) {
        return HandlerResult::error(errorResponse("Password must be at least 6 characters"), 400);
    }

    std::string hash = PasswordHash::hash(password);
    if (!UsersDAO::instance().registerUser(account, hash)) {
        return HandlerResult::error(errorResponse("Account exists"), 409);
    }

    invalidateAggregateStatsCache();

    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

static HandlerResult login_too_many_attempts_response(int login_window_seconds) {
    Document resp;
    resp.SetObject();
    resp.AddMember("error", "Too many failed attempts. Please try again later.", resp.GetAllocator());
    resp.AddMember("retry_after_seconds", login_window_seconds, resp.GetAllocator());
    return HandlerResult::error(docToString(resp), 429);
}

HandlerResult handleLogin(const std::string& account, const std::string& password, const std::string& client_ip) {
    if (account.empty() || password.empty()) {
        return HandlerResult::error(errorResponse("Account and password are required"), 400);
    }

    const int MAX_LOGIN_ATTEMPTS = Config::instance().getInt("security.max_login_attempts", 5);
    const int MAX_LOGIN_PER_IP = Config::instance().getInt("security.max_login_attempts_per_ip", 30);
    const int LOGIN_WINDOW_SECONDS = Config::instance().getInt("security.login_window_seconds", 900);
    std::string rate_limit_account_key = "login:" + account;
    std::string peer = client_ip.empty() ? "unknown" : client_ip;
    std::string rate_limit_ip_key = "login:ip:" + peer;

    if (!RateLimiter::checkAndRecord(rate_limit_account_key, MAX_LOGIN_ATTEMPTS, LOGIN_WINDOW_SECONDS)) {
        LOG_WARN("[Login] Rate limit exceeded for account: " + account);
        return login_too_many_attempts_response(LOGIN_WINDOW_SECONDS);
    }
    if (!RateLimiter::checkAndRecord(rate_limit_ip_key, MAX_LOGIN_PER_IP, LOGIN_WINDOW_SECONDS)) {
        LOG_WARN("[Login] Rate limit exceeded for IP: " + peer);
        return login_too_many_attempts_response(LOGIN_WINDOW_SECONDS);
    }

    std::string storedHash;
    int user_id = UsersDAO::instance().loginUser(account, storedHash);
    if (user_id == 0) {
        return HandlerResult::error(errorResponse("Invalid credentials"), 401);
    }
    if (user_id < 0) {
        return HandlerResult::error(errorResponse("Database error"), 500);
    }

    bool passwordValid = false;
    if (storedHash.find("pbkdf2_sha256$") == 0) {
        passwordValid = PasswordHash::verify(password, storedHash);
    } else {
        std::string computed = sha256(PASSWORD_SALT + password);
        passwordValid = (computed == storedHash);
        if (passwordValid) {
            std::string newHash = PasswordHash::hash(password);
            UsersDAO::instance().updatePasswordHash(user_id, newHash);
            LOG_INFO("[Login] Password hash upgraded to PBKDF2: " + account);
        }
    }

    if (!passwordValid) {
        int remaining = RateLimiter::getRemainingAttempts(rate_limit_account_key, MAX_LOGIN_ATTEMPTS, LOGIN_WINDOW_SECONDS);
        LOG_WARN("[Login] Failed: " + account + ", remaining: " + std::to_string(remaining));
        return HandlerResult::error(errorResponse("Invalid credentials"), 401);
    }

    RateLimiter::reset(rate_limit_account_key);
    RateLimiter::reset(rate_limit_ip_key);
    std::string token = Auth::generateToken(user_id, "");
    if (token.empty()) {
        return HandlerResult::error(errorResponse("Failed to generate authentication token"), 500);
    }

    Document resp;
    resp.SetObject();
    resp.AddMember("token", StringRef(token.c_str()), resp.GetAllocator());
    resp.AddMember("user_id", user_id, resp.GetAllocator());
    resp.AddMember("account", StringRef(account.c_str()), resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleUpload(int user_id, const std::string& filename, const std::vector<unsigned char>& file_data, const std::string& content_type) {
    auto uploadStart = std::chrono::steady_clock::now();
    if (file_data.empty()) {
        return HandlerResult::error(errorResponse("Empty file not allowed"), 400);
    }
    size_t maxSize = static_cast<size_t>(Config::instance().getInt("max_file_size", 100 * 1024 * 1024));
    if (file_data.size() > maxSize) {
        return HandlerResult::error(errorResponse("File too large"), 413);
    }

    // 检查用户存储配额
    long long userQuota = static_cast<long long>(Config::instance().getInt("user_quota_bytes", 1073741824));
    long long userUsage = FileMetaDAO::instance().getUserStorageUsage(user_id);
    if (userUsage + static_cast<long long>(file_data.size()) > userQuota) {
        return HandlerResult::error(errorResponse("Storage quota exceeded"), 413);
    }

    std::string ext = getFileExtension(filename);
    if (!isAllowedExtension(ext, ALLOWED_EXTENSIONS)) {
        return HandlerResult::error(errorResponse("File type not allowed"), 415);
    }

    std::string file_id = generateUUID();
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
    meta.upload_time = time(nullptr);
    meta.is_public = false;

    if (isImage(detected_mime)) {
        int w = 0, h = 0;
        if (ImageProcessor::getImageSize(file_data.data(), file_data.size(), w, h)) {
            meta.width = w;
            meta.height = h;
        }
    }

    bool minioOk = uploadFileToMinIO(file_id, file_data, detected_mime);
    if (!minioOk) {
        return HandlerResult::error(errorResponse("MinIO upload failed"), 500);
    }

    if (!FileMetaDAO::instance().save(meta)) {
        MinIOClient::instance().deleteObject(file_id);
        return HandlerResult::error(errorResponse("Failed to save metadata"), 500);
    }
    invalidateUserFilesCache(user_id);

    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - uploadStart).count();
    LOG_INFO("[Upload] " + filename + " (" + std::to_string(file_data.size()) + " bytes) - total:" +
             std::to_string(totalMs) + "ms");
    MetricsCollector::instance().recordBytes(true, file_data.size());

    std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600);
    if (presignUrl.empty()) {
        LOG_WARN("[Upload] presignGetUrl failed for " + file_id);
    }
    std::string downloadUrl = Config::instance().getString("minio.public_url") + file_id;
    Document resp; resp.SetObject();
    resp.AddMember("file_id", StringRef(file_id.c_str()), resp.GetAllocator());
    if (!presignUrl.empty()) {
        resp.AddMember("presign_url", StringRef(presignUrl.c_str()), resp.GetAllocator());
    }
    resp.AddMember("filename", StringRef(filename.c_str()), resp.GetAllocator());
    resp.AddMember("size", static_cast<uint64_t>(file_data.size()), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(detected_mime.c_str()), resp.GetAllocator());
    resp.AddMember("download_url", StringRef(downloadUrl.c_str()), resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleListFiles(int user_id, int offset, int limit, const std::string& search_keyword) {
    if (offset < 0) offset = 0;
    static const int MAX_PAGE_SIZE = Config::instance().getInt("files_list.max_page_size", 100);
    if (limit <= 0) limit = 20;
    if (limit > MAX_PAGE_SIZE) limit = MAX_PAGE_SIZE;

    std::string kw = search_keyword;
    static const int maxKwLen = Config::instance().getInt("files_list.max_search_keyword_length", 128);
    if (maxKwLen > 0 && static_cast<int>(kw.size()) > maxKwLen) {
        kw.resize(static_cast<size_t>(maxKwLen));
    }

    std::string cacheKey = "user_files:" + std::to_string(user_id) + ":" +
                           std::to_string(offset) + ":" + std::to_string(limit) + ":" + kw;

    struct FilesCacheEntry {
        std::string data;
        std::string key;
    };
    static std::atomic<int64_t> filesLocalCacheTimeMs{0};
    static std::shared_ptr<const FilesCacheEntry> filesLocalCache;

    {
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t cachedTime = filesLocalCacheTimeMs.load(std::memory_order_acquire);
        if (cachedTime > 0 && (nowMs - cachedTime) < 500) {
            auto cached = std::atomic_load(&filesLocalCache);
            if (cached && cached->key == cacheKey && !cached->data.empty()) {
                MetricsCollector::instance().recordCacheHit("lru", true);
                return HandlerResult::ok(cached->data);
            }
        }
    }

    std::string cached = RedisClient::instance().get(cacheKey);
    if (!cached.empty()) {
        MetricsCollector::instance().recordCacheHit("redis", true);
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        auto entry = std::make_shared<const FilesCacheEntry>(FilesCacheEntry{cached, cacheKey});
        std::atomic_store(&filesLocalCache, entry);
        filesLocalCacheTimeMs.store(nowMs, std::memory_order_release);
        return HandlerResult::ok(cached);
    }
    MetricsCollector::instance().recordCacheHit("redis", false);

    auto [files, total] = FileMetaDAO::instance().listAndCountByUserWithSearch(user_id, kw, offset, limit);

    static const std::string publicUrl = Config::instance().getString("minio.public_url");

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
        std::string pubUrl = publicUrl + f.file_id;
        Value downloadUrlVal;
        downloadUrlVal.SetString(pubUrl.c_str(), pubUrl.size(), resp.GetAllocator());
        obj.AddMember("download_url", downloadUrlVal, resp.GetAllocator());
        bool needsPreview = (f.mime_type.find("pdf") != std::string::npos ||
                             f.mime_type.find("video") != std::string::npos ||
                             f.mime_type.find("audio") != std::string::npos);
        obj.AddMember("needs_preview", needsPreview, resp.GetAllocator());
        bool isImg = (f.mime_type.find("image/") != std::string::npos);
        if (isImg) {
            std::string imgUrl = publicUrl + f.file_id;
            Value imgUrlVal;
            imgUrlVal.SetString(imgUrl.c_str(), imgUrl.size(), resp.GetAllocator());
            obj.AddMember("image_url", imgUrlVal, resp.GetAllocator());
        }
        filesArr.PushBack(obj, resp.GetAllocator());
    }
    resp.AddMember("files", filesArr, resp.GetAllocator());
    resp.AddMember("total", total, resp.GetAllocator());
    std::string result = docToString(resp);

    RedisClient::instance().setex(cacheKey, result, 30);
    std::string indexKey = "user_files_idx:" + std::to_string(user_id);
    RedisClient::instance().sadd(indexKey, cacheKey);
    RedisClient::instance().expire(indexKey, 60);

    return HandlerResult::ok(result);
}

static void invalidateAggregateStatsCache() {
    RedisClient::instance().del("cache:stats:aggregate");
}

void invalidateUserFilesCache(int user_id) {
    std::string indexKey = "user_files_idx:" + std::to_string(user_id);
    auto keys = RedisClient::instance().smembers(indexKey);
    RedisClient::instance().del(indexKey);
    if (!keys.empty()) {
        RedisClient::instance().delBatch(keys);
    }
    invalidateAggregateStatsCache();
}

HandlerResult handleDeleteFile(int user_id, const std::string& file_id) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        return HandlerResult::error(errorResponse("File not found"), 404);
    }
    if (meta.user_id != user_id) {
        return HandlerResult::error(errorResponse("Permission denied"), 403);
    }
    if (!FileMetaDAO::instance().del(file_id)) {
        return HandlerResult::error(errorResponse("Failed to delete metadata"), 500);
    }
    // 异步清理存储对象，不阻塞响应
    AeroQueue::instance().post([file_id]() {
        MinIOClient::instance().deleteObject(file_id);
        MinIOClient::instance().deleteObject(file_id + "_watermark");
        MinIOClient::instance().deleteObject("thumbs/" + file_id + "_200");
    });
    invalidateUserFilesCache(user_id);
    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleBatchDeleteFiles(int user_id, const std::vector<std::string>& file_ids) {
    if (file_ids.empty()) {
        Document resp; resp.SetObject();
        resp.AddMember("status", "success", resp.GetAllocator());
        resp.AddMember("deleted_count", 0, resp.GetAllocator());
        return HandlerResult::ok(docToString(resp));
    }

    // 限制单次批量删除数量，防止资源耗尽
    constexpr size_t MAX_BATCH_DELETE = 100;
    size_t batchSize = std::min(file_ids.size(), MAX_BATCH_DELETE);
    std::vector<std::string> trimmed_ids(file_ids.begin(), file_ids.begin() + batchSize);

    auto valid_ids = FileMetaDAO::instance().getValidFileIds(trimmed_ids, user_id);

    if (!valid_ids.empty()) {
        FileMetaDAO::instance().deleteFilesBatch(valid_ids);
    }

    // 异步清理 MinIO 对象（不阻塞响应）
    AeroQueue::instance().post([valid_ids]() {
        for (const auto& fid : valid_ids) {
            MinIOClient::instance().deleteObject(fid);
            MinIOClient::instance().deleteObject(fid + "_watermark");
            MinIOClient::instance().deleteObject("thumbs/" + fid + "_200");
        }
    });

    int success_count = static_cast<int>(valid_ids.size());
    int fail_count = static_cast<int>(file_ids.size()) - success_count;

    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("deleted_count", success_count, resp.GetAllocator());
    resp.AddMember("failed_count", fail_count, resp.GetAllocator());
    invalidateUserFilesCache(user_id);
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleShare(const std::string& file_id) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        return HandlerResult::error(errorResponse("File not found"), 404);
    }
    if (!meta.is_public) {
        return HandlerResult::error(errorResponse("File is private"), 403);
    }
    std::string url = Config::instance().getString("minio.public_url") + file_id;
    Document resp; resp.SetObject();
    resp.AddMember("share_url", StringRef(url.c_str()), resp.GetAllocator());
    resp.AddMember("filename", StringRef(meta.filename.c_str()), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(meta.mime_type.c_str()), resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleSetPublic(int user_id, const std::string& file_id) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        return HandlerResult::error(errorResponse("File not found"), 404);
    }
    if (meta.user_id != user_id) {
        return HandlerResult::error(errorResponse("Permission denied"), 403);
    }
    bool new_public = !meta.is_public;
    if (!FileMetaDAO::instance().updatePublic(file_id, new_public)) {
        return HandlerResult::error(errorResponse("Failed to update metadata"), 500);
    }
    invalidateUserFilesCache(user_id);
    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("is_public", new_public, resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleSetPublic(int user_id, const std::string& file_id, bool is_public) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        return HandlerResult::error(errorResponse("File not found"), 404);
    }
    if (meta.user_id != user_id) {
        return HandlerResult::error(errorResponse("Permission denied"), 403);
    }
    if (!FileMetaDAO::instance().updatePublic(file_id, is_public)) {
        return HandlerResult::error(errorResponse("Failed to update metadata"), 500);
    }
    invalidateUserFilesCache(user_id);
    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("is_public", is_public, resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleGetPresignUrl(int user_id, const std::string& file_id) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        return HandlerResult::error(errorResponse("File not found"), 404);
    }
    if (!meta.is_public && meta.user_id != user_id) {
        return HandlerResult::error(errorResponse("Permission denied"), 403);
    }
    std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600);
    Document resp; resp.SetObject();
    resp.AddMember("presign_url", StringRef(presignUrl.c_str()), resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleRequestUploadUrl(int user_id, const std::string& filename,
                                     const std::string& content_type, size_t file_size) {
    if (file_size == 0) {
        return HandlerResult::error(errorResponse("Empty file not allowed"), 400);
    }
    size_t maxSize = static_cast<size_t>(Config::instance().getInt("max_file_size", 100 * 1024 * 1024));
    if (file_size > maxSize) {
        return HandlerResult::error(errorResponse("File too large"), 413);
    }

    std::string ext = getFileExtension(filename);
    if (!isAllowedExtension(ext, ALLOWED_EXTENSIONS)) {
        return HandlerResult::error(errorResponse("File type not allowed"), 415);
    }

    // 配额检查
    long long userQuota = static_cast<long long>(Config::instance().getInt("user_quota_bytes", 1073741824));
    long long userUsage = FileMetaDAO::instance().getUserStorageUsage(user_id);
    if (userUsage + static_cast<long long>(file_size) > userQuota) {
        return HandlerResult::error(errorResponse("Storage quota exceeded"), 413);
    }

    std::string file_id = generateUUID();
    std::string detectedMime = getMimeTypeFromExtension(filename);
    int expires = 7200;
    std::string presignUrl = MinIOClient::instance().presignPutUrl(file_id, expires);
    if (presignUrl.empty()) {
        return HandlerResult::error(errorResponse("Failed to generate upload URL"), 500);
    }

    Document resp; resp.SetObject();
    resp.AddMember("file_id", StringRef(file_id.c_str()), resp.GetAllocator());
    resp.AddMember("upload_url", StringRef(presignUrl.c_str()), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(detectedMime.c_str()), resp.GetAllocator());
    resp.AddMember("expires_in", expires, resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleConfirmUpload(int user_id, const std::string& file_id,
                                  const std::string& filename,
                                  const std::string& content_type,
                                  size_t file_size) {
    if (!MinIOClient::instance().objectExists(file_id)) {
        return HandlerResult::error(errorResponse("File not found in storage"), 404);
    }

    std::string mime_type = content_type.empty() ? getMimeTypeFromExtension(filename) : content_type;

    FileMeta meta;
    meta.file_id = file_id;
    meta.user_id = user_id;
    meta.filename = filename;
    meta.size = file_size;
    meta.mime_type = mime_type;
    meta.upload_time = time(nullptr);
    meta.is_public = false;

    // 仅读取前 64KB 获取图片尺寸，避免大文件全量下载
    if (isImage(mime_type)) {
        std::vector<char> headerData;
        if (MinIOClient::instance().getObjectRange(file_id, 0, 65536, headerData) && !headerData.empty()) {
            int w = 0, h = 0;
            if (ImageProcessor::getImageSize(headerData, w, h)) {
                meta.width = w;
                meta.height = h;
            }
        }
    }

    if (!FileMetaDAO::instance().save(meta)) {
        return HandlerResult::error(errorResponse("Failed to save metadata"), 500);
    }
    invalidateUserFilesCache(user_id);

    std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600);
    std::string downloadUrl = Config::instance().getString("minio.public_url") + file_id;
    Document resp; resp.SetObject();
    resp.AddMember("file_id", StringRef(file_id.c_str()), resp.GetAllocator());
    resp.AddMember("presign_url", StringRef(presignUrl.c_str()), resp.GetAllocator());
    resp.AddMember("filename", StringRef(filename.c_str()), resp.GetAllocator());
    resp.AddMember("size", static_cast<uint64_t>(file_size), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(mime_type.c_str()), resp.GetAllocator());
    resp.AddMember("download_url", StringRef(downloadUrl.c_str()), resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleStats() {
    static const int statsTtl = Config::instance().getInt("stats_cache_ttl_seconds", 60);
    static const char* kStatsCacheKey = "cache:stats:aggregate";

    static std::atomic<int64_t> localCacheTimeMs{0};
    static std::shared_ptr<const std::string> localCache;

    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t cachedTime = localCacheTimeMs.load(std::memory_order_acquire);
    if (cachedTime > 0 && (nowMs - cachedTime) < 1000) {
        auto cached = std::atomic_load(&localCache);
        if (cached && !cached->empty()) {
            return HandlerResult::ok(*cached);
        }
    }

    if (statsTtl > 0) {
        std::string cached = RedisClient::instance().get(kStatsCacheKey);
        if (!cached.empty()) {
            std::atomic_store(&localCache, std::make_shared<const std::string>(cached));
            localCacheTimeMs.store(nowMs, std::memory_order_release);
            return HandlerResult::ok(cached);
        }
    }

    int total_users = 0, total_files = 0, total_images = 0;
    long long total_size = 0;
    FileMetaDAO::instance().getFileStats(total_files, total_images, total_size);
    total_users = UsersDAO::instance().getUserCount();

    Document resp; resp.SetObject();
    resp.AddMember("total_users", total_users, resp.GetAllocator());
    resp.AddMember("total_files", total_files, resp.GetAllocator());
    resp.AddMember("total_images", total_images, resp.GetAllocator());
    resp.AddMember("total_size", static_cast<int64_t>(total_size), resp.GetAllocator());
    std::string result = docToString(resp);
    if (statsTtl > 0) {
        RedisClient::instance().setex(kStatsCacheKey, result, statsTtl);
    }
    std::atomic_store(&localCache, std::make_shared<const std::string>(result));
    localCacheTimeMs.store(nowMs, std::memory_order_release);
    return HandlerResult::ok(result);
}

HandlerResult handleSendVerificationCode(const std::string& email, const std::string& client_ip) {
    if (email.empty()) {
        return HandlerResult::error(errorResponse("Email is required"), 400);
    }

    auto atPos = email.find('@');
    auto lastDot = email.rfind('.');
    if (atPos == std::string::npos || lastDot == std::string::npos || atPos == 0 || atPos >= lastDot || lastDot == email.size() - 1) {
        return HandlerResult::error(errorResponse("Invalid email format"), 400);
    }

    const int SEND_MAX = Config::instance().getInt("security.max_send_code_requests_per_ip_per_hour", 10);
    const int SEND_WINDOW = Config::instance().getInt("security.send_code_ip_window_seconds", 3600);
    std::string peer = client_ip.empty() ? "unknown" : client_ip;
    if (!RateLimiter::allowConsume("sendcode:" + peer, SEND_MAX, SEND_WINDOW)) {
        LOG_WARN("[SendCode] Rate limit exceeded for IP: " + peer);
        Document resp;
        resp.SetObject();
        resp.AddMember("error", "Too many verification code requests from this network. Please try again later.", resp.GetAllocator());
        resp.AddMember("retry_after_seconds", SEND_WINDOW, resp.GetAllocator());
        return HandlerResult::error(docToString(resp), 429);
    }

    std::string code = EmailSender::generateVerificationCode(6);
    if (!EmailVerificationDAO::instance().save(email, code)) {
        return HandlerResult::error(errorResponse("Failed to save verification code"), 500);
    }

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
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleEmailRegister(const std::string& account, const std::string& password, const std::string& email, const std::string& code) {
    if (account.empty() || password.empty() || email.empty() || code.empty()) {
        return HandlerResult::error(errorResponse("All fields are required"), 400);
    }
    if (account.length() < 3 || account.length() > 64) {
        return HandlerResult::error(errorResponse("Account must be 3-64 characters"), 400);
    }
    if (password.length() < 6) {
        return HandlerResult::error(errorResponse("Password must be at least 6 characters"), 400);
    }

    auto atPos = email.find('@');
    auto lastDot = email.rfind('.');
    if (atPos == std::string::npos || lastDot == std::string::npos || atPos == 0 || atPos >= lastDot || lastDot == email.size() - 1) {
        return HandlerResult::error(errorResponse("Invalid email format"), 400);
    }

    if (!EmailVerificationDAO::instance().verifyCode(email, code)) {
        return HandlerResult::error(errorResponse("Invalid or expired verification code"), 400);
    }
    if (EmailVerificationDAO::instance().isCodeUsed(email, code)) {
        return HandlerResult::error(errorResponse("Verification code already used"), 400);
    }

    TransactionGuard guard(ConnectionPool::getInstance().getConnection());
    if (!guard) {
        return HandlerResult::error(errorResponse("Database error"), 500);
    }
    MYSQL* conn = guard.get();

    if (UsersDAO::instance().emailExists(conn, email)) {
        return HandlerResult::error(errorResponse("Email already registered"), 409);
    }

    std::string hash = PasswordHash::hash(password);
    int ret = UsersDAO::instance().registerUserWithEmail(conn, account, hash, email);
    if (ret != 0) {
        return HandlerResult::error(errorResponse(ret == -1 ? "Account or email already exists" : "Registration failed"), ret == -1 ? 409 : 500);
    }

    // 先标记验证码已使用，再提交事务，防止事务提交后崩溃导致验证码复用
    EmailVerificationDAO::instance().markCodeAsUsed(email, code);
    guard.commit();

    invalidateAggregateStatsCache();

    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("message", "Registration successful", resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleMultipartInit(int user_id, const std::string& filename,
                                  const std::string& content_type, size_t file_size) {
    if (file_size == 0) {
        return HandlerResult::error(errorResponse("Empty file not allowed"), 400);
    }
    size_t maxSize = static_cast<size_t>(Config::instance().getInt("max_file_size", 100 * 1024 * 1024));
    if (file_size > maxSize) {
        return HandlerResult::error(errorResponse("File too large"), 413);
    }

    std::string ext = getFileExtension(filename);
    if (!isAllowedExtension(ext, ALLOWED_EXTENSIONS)) {
        return HandlerResult::error(errorResponse("File type not allowed"), 415);
    }

    std::string upload_id = generateUUID();
    size_t chunk_size = 20 * 1024 * 1024;
    int total_chunks = static_cast<int>((file_size + chunk_size - 1) / chunk_size);

    RedisClient::instance().setex("upload_active:" + upload_id, std::to_string(total_chunks), 86400);
    RedisClient::instance().setex("upload_owner:" + upload_id, std::to_string(user_id), 86400);
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
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleMultipartUploadChunk(int user_id, const std::string& upload_id, int part_number,
                                         const std::vector<unsigned char>& data) {
    if (upload_id.empty() || data.empty() || part_number < 0) {
        LOG_ERROR("[Chunk] invalid params: uid=" + std::to_string(user_id) + " id=" + upload_id + " part=" + std::to_string(part_number));
        return HandlerResult::error(errorResponse("Invalid upload_id, part_number or empty chunk"), 400);
    }

    std::string activeVal = RedisClient::instance().get("upload_active:" + upload_id);
    if (activeVal.empty()) {
        return HandlerResult::error(errorResponse("Invalid or expired upload_id"), 400);
    }

    std::string ownerStr = RedisClient::instance().get("upload_owner:" + upload_id);
    if (!ownerStr.empty()) {
        int owner_id = 0;
        try { owner_id = std::stoi(ownerStr); } catch (...) {}
        if (owner_id != user_id) {
            return HandlerResult::error(errorResponse("Permission denied"), 403);
        }
    }

    // 限制单个 chunk 大小（默认 25MB）
    size_t maxChunkSize = static_cast<size_t>(Config::instance().getInt("upload.max_chunk_size", 25 * 1024 * 1024));
    if (data.size() > maxChunkSize) {
        return HandlerResult::error(errorResponse("Chunk too large"), 413);
    }

    std::string chunk_key = "chunks/" + upload_id + "/" + std::to_string(part_number);
    if (!MinIOClient::instance().putObject(chunk_key, data, "application/octet-stream")) {
        LOG_ERROR("[Chunk] MinIO putObject failed: " + chunk_key);
        return HandlerResult::error(errorResponse("Failed to upload chunk to storage"), 500);
    }

    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("part_number", part_number, resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleMultipartComplete(int user_id, const std::string& upload_id,
                                      const std::string& filename, const std::string& content_type,
                                      size_t file_size, int total_chunks) {
    if (upload_id.empty() || total_chunks <= 0 || total_chunks > 10000) {
        return HandlerResult::error(errorResponse("Invalid upload_id or total_chunks"), 400);
    }

    std::string activeVal = RedisClient::instance().get("upload_active:" + upload_id);
    if (activeVal.empty()) {
        return HandlerResult::error(errorResponse("Invalid or expired upload_id"), 400);
    }

    std::string ownerStr = RedisClient::instance().get("upload_owner:" + upload_id);
    if (!ownerStr.empty()) {
        int owner_id = 0;
        try { owner_id = std::stoi(ownerStr); } catch (...) {}
        if (owner_id != user_id) {
            return HandlerResult::error(errorResponse("Permission denied"), 403);
        }
    }

    // 配额检查
    long long userQuota = static_cast<long long>(Config::instance().getInt("user_quota_bytes", 1073741824));
    long long userUsage = FileMetaDAO::instance().getUserStorageUsage(user_id);
    if (userUsage + static_cast<long long>(file_size) > userQuota) {
        return HandlerResult::error(errorResponse("Storage quota exceeded"), 413);
    }

    std::vector<std::string> chunk_keys;
    for (int i = 0; i < total_chunks; ++i) {
        chunk_keys.push_back("chunks/" + upload_id + "/" + std::to_string(i));
    }
    if (chunk_keys.empty()) {
        return HandlerResult::error(errorResponse("No chunks found for upload"), 400);
    }

    std::vector<std::string> missingChunks;
    for (const auto& key : chunk_keys) {
        if (!MinIOClient::instance().objectExists(key)) {
            missingChunks.push_back(key);
        }
    }
    if (!missingChunks.empty()) {
        LOG_ERROR("[Multipart] Missing chunks for upload_id=" + upload_id +
                  ", missing=" + std::to_string(missingChunks.size()));
        for (const auto& key : chunk_keys) {
            MinIOClient::instance().deleteObject(key);
        }
        Document resp; resp.SetObject();
        resp.AddMember("error", "Incomplete upload: missing chunks", resp.GetAllocator());
        resp.AddMember("missing_count", static_cast<int>(missingChunks.size()), resp.GetAllocator());
        return HandlerResult::error(docToString(resp), 400);
    }

    std::string file_id = generateUUID();
    std::string detected_mime = content_type.empty() ? getMimeTypeFromExtension(filename) : content_type;

    if (!MinIOClient::instance().composeObjects(file_id, detected_mime, chunk_keys)) {
        return HandlerResult::error(errorResponse("Failed to compose chunks"), 500);
    }

    // 验证合并后的文件大小
    if (file_size > 0) {
        // composeObjects 不保证文件大小一致，但这里可以简单验证
        // 由于 MinIO compose 后无法直接获取大小，跳过此检查
    }

    FileMeta meta;
    meta.file_id = file_id;
    meta.user_id = user_id;
    meta.filename = filename;
    meta.size = file_size;
    meta.mime_type = detected_mime;
    meta.upload_time = time(nullptr);
    meta.is_public = false;

    if (isImage(detected_mime)) {
        std::vector<char> firstChunk;
        std::string firstChunkKey = "chunks/" + upload_id + "/0";
        // 仅读取前 64KB 获取图片尺寸，避免大 chunk 全量下载
        if (MinIOClient::instance().getObjectRange(firstChunkKey, 0, 65536, firstChunk) && !firstChunk.empty()) {
            int w = 0, h = 0;
            if (ImageProcessor::getImageSize(firstChunk, w, h)) {
                meta.width = w;
                meta.height = h;
            }
        }
    }

    if (!FileMetaDAO::instance().save(meta)) {
        MinIOClient::instance().deleteObject(file_id);
        return HandlerResult::error(errorResponse("Failed to save metadata"), 500);
    }
    invalidateUserFilesCache(user_id);

    RedisClient::instance().del("upload_active:" + upload_id);
    untrackMultipartUpload(upload_id);

    AeroQueue::instance().post([chunk_keys]() {
        for (const auto& key : chunk_keys) {
            MinIOClient::instance().deleteObject(key);
        }
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
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleMultipartCleanup(int user_id, const std::string& upload_id) {
    if (upload_id.empty()) {
        return HandlerResult::error(errorResponse("upload_id is required"), 400);
    }

    // 验证 upload_id 归属
    std::string ownerKey = "upload_owner:" + upload_id;
    std::string ownerStr = RedisClient::instance().get(ownerKey);
    if (ownerStr.empty()) {
        return HandlerResult::error(errorResponse("Invalid or expired upload_id"), 400);
    }
    int owner_id = 0;
    try { owner_id = std::stoi(ownerStr); } catch (...) {}
    if (owner_id != user_id) {
        return HandlerResult::error(errorResponse("Permission denied"), 403);
    }

    // 验证 upload_id 是否有效
    std::string totalStr = RedisClient::instance().get("upload_active:" + upload_id);
    if (totalStr.empty()) {
        return HandlerResult::error(errorResponse("Invalid or expired upload_id"), 400);
    }

    int total_chunks = 0;
    if (!totalStr.empty()) {
        try { total_chunks = std::stoi(totalStr); } catch (...) {}
    }
    if (total_chunks <= 0 || total_chunks > 10000) {
        RedisClient::instance().del("upload_active:" + upload_id);
        RedisClient::instance().del("upload_owner:" + upload_id);
        return HandlerResult::error(errorResponse("Invalid upload metadata, cleaned up"), 400);
    }

    for (int i = 0; i < total_chunks; ++i) {
        std::string chunk_key = "chunks/" + upload_id + "/" + std::to_string(i);
        if (!MinIOClient::instance().objectExists(chunk_key)) continue;
        MinIOClient::instance().deleteObject(chunk_key);
    }
    untrackMultipartUpload(upload_id);
    RedisClient::instance().del("upload_active:" + upload_id);
    RedisClient::instance().del("upload_owner:" + upload_id);
    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

void cleanupOrphanChunks() {
    LOG_INFO("[Cleanup] Starting orphan chunk cleanup...");
    auto members = RedisClient::instance().smembers("active_multipart_uploads");
    if (members.empty()) {
        LOG_INFO("[Cleanup] No active multipart uploads");
        return;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int MAX_AGE_SECONDS = Config::instance().getInt("cleanup.multipart_ttl_seconds", 86400);
    int cleaned = 0;

    for (const auto& member : members) {
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

        if ((now - timestamp) < MAX_AGE_SECONDS) continue;

        LOG_WARN("[Cleanup] Cleaning orphaned upload: " + upload_id +
                 " (age=" + std::to_string(now - timestamp) + "s)");

        for (int i = 0; i < total_chunks; ++i) {
            std::string key = "chunks/" + upload_id + "/" + std::to_string(i);
            MinIOClient::instance().deleteObject(key);
        }

        RedisClient::instance().srem("active_multipart_uploads", member);
        cleaned++;
    }

    LOG_INFO("[Cleanup] Completed, cleaned=" + std::to_string(cleaned) +
             " uploads, tracked=" + std::to_string(members.size()));
}

// ========== 水印相关 ==========

HandlerResult handleAddWatermark(int user_id, const std::string& file_id,
                                  const std::string& text, const std::string& position, int opacity) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty() || meta.user_id != user_id) {
        return HandlerResult::error(errorResponse("File not found"), 404);
    }
    if (meta.mime_type.find("image/") != 0) {
        return HandlerResult::error(errorResponse("Watermark only supports images"), 400);
    }
    MinIOClient::instance().deleteObject(file_id + "_watermark");
    if (!FileMetaDAO::instance().setWatermark(file_id, text, position, opacity)) {
        return HandlerResult::error(errorResponse("Failed to update metadata"), 500);
    }
    RedisClient::instance().del("file_meta:" + file_id);
    invalidateUserFilesCache(user_id);
    Document resp; resp.SetObject();
    resp.AddMember("success", true, resp.GetAllocator());
    resp.AddMember("message", StringRef("Watermark added successfully"), resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleRemoveWatermark(int user_id, const std::string& file_id) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty() || meta.user_id != user_id) {
        return HandlerResult::error(errorResponse("File not found"), 404);
    }
    std::string watermarkKey = file_id + "_watermark";
    MinIOClient::instance().deleteObject(watermarkKey);
    if (!FileMetaDAO::instance().clearWatermark(file_id)) {
        return HandlerResult::error(errorResponse("Failed to clear watermark"), 500);
    }
    RedisClient::instance().del("file_meta:" + file_id);
    invalidateUserFilesCache(user_id);
    Document resp; resp.SetObject();
    resp.AddMember("success", true, resp.GetAllocator());
    resp.AddMember("message", StringRef("Watermark removed successfully"), resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleGetWatermarkConfig(int user_id, const std::string& file_id) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty() || meta.user_id != user_id) {
        return HandlerResult::error(errorResponse("File not found"), 404);
    }
    std::string text, position;
    int opacity;
    bool hasWatermark = FileMetaDAO::instance().getWatermark(file_id, text, position, opacity);
    Document resp; resp.SetObject();
    resp.AddMember("has_watermark", hasWatermark, resp.GetAllocator());
    if (hasWatermark) {
        resp.AddMember("text", StringRef(text.c_str()), resp.GetAllocator());
        resp.AddMember("position", StringRef(position.c_str()), resp.GetAllocator());
        resp.AddMember("opacity", opacity, resp.GetAllocator());
    } else {
        resp.AddMember("text", "", resp.GetAllocator());
        resp.AddMember("position", "bottom-right", resp.GetAllocator());
        resp.AddMember("opacity", 50, resp.GetAllocator());
    }
    return HandlerResult::ok(docToString(resp));
}
