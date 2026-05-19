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
#include <future>

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
    "jpg","jpeg","png","gif","webp","pdf","doc","docx","xls","xlsx",
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

    if (!RateLimiter::isAllowed(rate_limit_account_key, MAX_LOGIN_ATTEMPTS, LOGIN_WINDOW_SECONDS)) {
        LOG_WARN("[Login] Rate limit exceeded for account: " + account);
        return login_too_many_attempts_response(LOGIN_WINDOW_SECONDS);
    }
    if (!RateLimiter::isAllowed(rate_limit_ip_key, MAX_LOGIN_PER_IP, LOGIN_WINDOW_SECONDS)) {
        LOG_WARN("[Login] Rate limit exceeded for IP: " + peer);
        return login_too_many_attempts_response(LOGIN_WINDOW_SECONDS);
    }

    std::string storedHash;
    int user_id = UsersDAO::instance().loginUser(account, storedHash);
    if (user_id == 0) {
        RateLimiter::recordFailure(rate_limit_account_key, LOGIN_WINDOW_SECONDS);
        RateLimiter::recordFailure(rate_limit_ip_key, LOGIN_WINDOW_SECONDS);
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
        RateLimiter::recordFailure(rate_limit_account_key, LOGIN_WINDOW_SECONDS);
        RateLimiter::recordFailure(rate_limit_ip_key, LOGIN_WINDOW_SECONDS);
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

    std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600);
    std::string downloadUrl = Config::instance().getString("minio.public_url") + file_id;
    Document resp; resp.SetObject();
    resp.AddMember("file_id", StringRef(file_id.c_str()), resp.GetAllocator());
    resp.AddMember("presign_url", StringRef(presignUrl.c_str()), resp.GetAllocator());
    resp.AddMember("filename", StringRef(filename.c_str()), resp.GetAllocator());
    resp.AddMember("size", static_cast<uint64_t>(file_data.size()), resp.GetAllocator());
    resp.AddMember("mime_type", StringRef(detected_mime.c_str()), resp.GetAllocator());
    resp.AddMember("download_url", StringRef(downloadUrl.c_str()), resp.GetAllocator());
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleListFiles(int user_id, int offset, int limit, const std::string& search_keyword) {
    std::string kw = search_keyword;
    const int maxKwLen = Config::instance().getInt("files_list.max_search_keyword_length", 128);
    if (maxKwLen > 0 && static_cast<int>(kw.size()) > maxKwLen) {
        kw.resize(static_cast<size_t>(maxKwLen));
    }

    std::string cacheKey = "user_files:" + std::to_string(user_id) + ":" +
                           std::to_string(offset) + ":" + std::to_string(limit) + ":" + kw;

    std::string cached = RedisClient::instance().get(cacheKey);
    if (!cached.empty()) {
        return HandlerResult::ok(cached);
    }

    auto files = FileMetaDAO::instance().listByUserWithSearch(user_id, kw, offset, limit);
    int total = FileMetaDAO::instance().countByUserWithSearch(user_id, kw);

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
        Value downloadUrlVal;
        downloadUrlVal.SetString(pubUrl.c_str(), pubUrl.size(), resp.GetAllocator());
        obj.AddMember("download_url", downloadUrlVal, resp.GetAllocator());
        std::string mime = f.mime_type;
        bool needsPreview = (mime.find("pdf") != std::string::npos ||
                             mime.find("video") != std::string::npos ||
                             mime.find("audio") != std::string::npos);
        obj.AddMember("needs_preview", needsPreview, resp.GetAllocator());
        bool isImg = (mime.find("image/") != std::string::npos);
        if (isImg) {
            std::string presign = MinIOClient::instance().presignGetUrl(f.file_id, 3600);
            Value presignVal;
            presignVal.SetString(presign.c_str(), presign.size(), resp.GetAllocator());
            obj.AddMember("presign_url", presignVal, resp.GetAllocator());
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
    if (!keys.empty()) {
        keys.push_back(indexKey);
        RedisClient::instance().delBatch(keys);
    } else {
        RedisClient::instance().del(indexKey);
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
    MinIOClient::instance().deleteObject(file_id);
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

    auto valid_ids = FileMetaDAO::instance().getValidFileIds(file_ids, user_id);

    constexpr size_t MAX_CONCURRENT_DELETES = 8;
    std::vector<std::future<bool>> deleteFutures;
    deleteFutures.reserve(valid_ids.size());
    for (const auto& fid : valid_ids) {
        if (deleteFutures.size() >= MAX_CONCURRENT_DELETES) {
            deleteFutures.front().get();
            deleteFutures.erase(deleteFutures.begin());
        }
        std::string key = fid;
        deleteFutures.push_back(std::async(std::launch::async, [key]() {
            return MinIOClient::instance().deleteObject(key);
        }));
    }
    for (auto& f : deleteFutures) f.get();

    if (!valid_ids.empty()) {
        FileMetaDAO::instance().deleteFilesBatch(valid_ids);
    }

    int success_count = static_cast<int>(valid_ids.size());
    int fail_count = static_cast<int>(file_ids.size()) - success_count;

    Document resp; resp.SetObject();
    resp.AddMember("status", "success", resp.GetAllocator());
    resp.AddMember("deleted_count", success_count, resp.GetAllocator());
    resp.AddMember("failed_count", fail_count, resp.GetAllocator());
    invalidateUserFilesCache(user_id);
    return HandlerResult::ok(docToString(resp));
}

std::pair<std::vector<char>, std::string> handleGetFile(const std::string& file_id, bool check_auth, int user_id, const std::string& user_agent) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) return { {}, "" };
    if (check_auth && !meta.is_public && meta.user_id != user_id) return { {}, "" };

    RedisClient::instance().incr("file_views:" + file_id);
    AeroQueue::instance().post([file_id]() {
        RedisClient::instance().sadd("file_views_keys", "file_views:" + file_id);
    });

    std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600);
    return { {}, presignUrl };
}

HandlerResult handleShare(const std::string& file_id) {
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        return HandlerResult::error(errorResponse("File not found"), 404);
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

    std::string file_id = generateUUID();
    std::string detectedMime = getMimeTypeFromExtension(filename);
    int expires = 7200;
    std::string presignUrl = MinIOClient::instance().presignPutUrl(file_id, expires);

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

    if (isImage(mime_type)) {
        std::vector<char> fileData;
        if (MinIOClient::instance().getObject(file_id, fileData) && !fileData.empty()) {
            int w = 0, h = 0;
            if (ImageProcessor::getImageSize(fileData, w, h)) {
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
    const int statsTtl = Config::instance().getInt("stats_cache_ttl_seconds", 60);
    static const char* kStatsCacheKey = "cache:stats:aggregate";

    if (statsTtl > 0) {
        std::string cached = RedisClient::instance().get(kStatsCacheKey);
        if (!cached.empty()) {
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

    guard.commit();
    EmailVerificationDAO::instance().markCodeAsUsed(email, code);

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
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleMultipartUploadChunk(const std::string& upload_id, int part_number,
                                         const std::vector<unsigned char>& data) {
    if (upload_id.empty() || data.empty()) {
        LOG_ERROR("[Chunk] empty upload_id or data: id=" + upload_id + " part=" + std::to_string(part_number));
        return HandlerResult::error(errorResponse("Invalid upload_id or empty chunk"), 400);
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
        if (MinIOClient::instance().getObject(firstChunkKey, firstChunk) && !firstChunk.empty()) {
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
    return HandlerResult::ok(docToString(resp));
}

HandlerResult handleMultipartCleanup(int user_id, const std::string& upload_id) {
    std::string totalStr = RedisClient::instance().get("upload_active:" + upload_id);
    int total_chunks = 1000;
    if (!totalStr.empty()) {
        try { total_chunks = std::stoi(totalStr); } catch (...) {}
    }

    for (int i = 0; i < total_chunks; ++i) {
        std::string chunk_key = "chunks/" + upload_id + "/" + std::to_string(i);
        if (!MinIOClient::instance().objectExists(chunk_key)) break;
        MinIOClient::instance().deleteObject(chunk_key);
    }
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

        std::vector<std::future<bool>> futures;
        futures.reserve(total_chunks);
        for (int i = 0; i < total_chunks; ++i) {
            std::string key = "chunks/" + upload_id + "/" + std::to_string(i);
            futures.push_back(std::async(std::launch::async, [key]() {
                return MinIOClient::instance().deleteObject(key);
            }));
        }
        for (auto& f : futures) f.get();

        RedisClient::instance().srem("active_multipart_uploads", member);
        cleaned++;
    }

    LOG_INFO("[Cleanup] Completed, cleaned=" + std::to_string(cleaned) +
             " uploads, tracked=" + std::to_string(members.size()));
}
