#include "FileMeta.hpp"
#include "HttpServer.hpp"
#include "Handlers.hpp"
#include "Auth.hpp"
#include "Log.hpp"
#include "MinIOClient.hpp"
#include "Utils.hpp"
#include "ImageProcessor.hpp"
#include "AeroQueue.hpp"
#include "Config.hpp"
#include <cpprest/http_listener.h>
#include <cpprest/json.h>
#include <cpprest/http_headers.h>
#include <cpprest/details/basic_types.h>

// 确保 U() 宏可用（cpprestsdk 在某些平台/版本中未自动引入）
#ifndef U
#ifdef _UTF16_STRINGS
#define U(x) L ## x
#else
#define U(x) x
#endif
#endif
#include <regex>
#include <boost/optional.hpp>
#include <utility>

using namespace web;
using namespace web::http;
using namespace web::http::experimental::listener;

// 获取 CORS 允许的域名，从配置文件读取，默认允许所有
static std::string getCorsOrigin() {
    return Config::instance().getString("security.cors_origin", "*");
}

void replyJsonWithCors(http_request request, status_code code, const json::value& json) {
    http_response response(code);
    response.headers().add(U("Access-Control-Allow-Origin"), utility::conversions::to_string_t(getCorsOrigin()));
    response.headers().add(U("Content-Type"), U("application/json; charset=utf-8"));
    response.set_body(json);
    request.reply(response);
}

void replyErrorWithCors(http_request request, status_code code, const utility::string_t& msg) {
    http_response response(code);
    response.headers().add(U("Access-Control-Allow-Origin"), utility::conversions::to_string_t(getCorsOrigin()));
    response.set_body(msg);
    request.reply(response);
}

HttpServer::HttpServer(int port) : port_(port) {
    std::string url = "http://0.0.0.0:" + std::to_string(port_) + "/api";
    listener_ = std::make_unique<http_listener>(utility::conversions::to_string_t(url));
    setupRoutes();
}

void HttpServer::setupRoutes() {
    listener_->support(methods::OPTIONS, std::bind(&HttpServer::handleOptions, this, std::placeholders::_1));
    listener_->support(std::bind(&HttpServer::handleAll, this, std::placeholders::_1));
}

void HttpServer::handleOptions(http_request request) {
    http_response response(status_codes::OK);
    auto origin = utility::conversions::to_string_t(getCorsOrigin());
    response.headers().add(U("Access-Control-Allow-Origin"), origin);
    response.headers().add(U("Access-Control-Allow-Methods"), U("GET, POST, PUT, DELETE, OPTIONS"));
    response.headers().add(U("Access-Control-Allow-Headers"), U("Content-Type, Authorization, X-File-Name, X-File-Size, X-File-Type"));
    response.headers().add(U("Access-Control-Expose-Headers"), U("*"));
    response.headers().add(U("Access-Control-Max-Age"), U("86400"));
    request.reply(response);
}

void HttpServer::start() {
    listener_->open().wait();
    LOG_INFO("HTTP server listening on port " + std::to_string(port_));
}

void HttpServer::handleAll(http_request request) {
    auto path = request.request_uri().path();
    std::string path_str = utility::conversions::to_utf8string(path);
    auto method = request.method();
    std::string method_str = utility::conversions::to_utf8string(method);

    LOG_INFO("handleAll: " + method_str + " " + path_str);

    if (method == methods::POST && path_str == "/api/auth/register") {
        handleRegister(request);
    }
    else if (method == methods::POST && path_str == "/api/auth/login") {
        handleLogin(request);
    }
    else if (method == methods::POST && path_str == "/api/upload/request") {
        handleRequestUploadUrl(request);
    }
    else if (method == methods::POST && path_str == "/api/upload/confirm") {
        handleConfirmUpload(request);
    }
    else if ((method == methods::POST || method == methods::PUT) && path_str == "/api/upload") {
        handleUpload(request);
    }
    else if (method == methods::POST && path_str == "/api/upload/presign") {
        handlePresignUpload(request);
    }
    else if (method == methods::POST && path_str == "/api/upload/confirm") {
        handleConfirmUploadRoute(request);
    }
    else if (method == methods::GET && path_str == "/api/files") {
        handleListFiles(request);
    }
    else if (method == methods::PUT && path_str.find("/api/file/") == 0 && path_str.find("/public") != std::string::npos) {
        handleSetPublic(request);
    }
    else if (method == methods::DEL && path_str.find("/api/file/") == 0) {
        handleDeleteFile(request);
    }
    else if (method == methods::POST && path_str == "/api/files/batch-delete") {
        handleBatchDeleteFiles(request);
    }
    else if (method == methods::GET && path_str.find("/api/i/") == 0) {
        handleGetFile(request);
    }
    else if (method == methods::POST && path_str.find("/api/share/") == 0) {
        handleShare(request);
    }
    else if (method == methods::POST && path_str == "/api/cleanup") {
        handleCleanup(request);
    }
    else if (method == methods::GET && path_str == "/api/stats") {
        handleStats(request);
    }
    else if (method == methods::POST && path_str == "/api/auth/register/email") {
        handleEmailRegister(request);
    }
    else if (method == methods::POST && path_str == "/api/auth/send-code") {
        handleSendVerificationCode(request);
    }
    else if (method == methods::POST && path_str == "/api/upload/multipart/init") {
        handleMultipartInit(request);
    }
    else if ((method == methods::POST || method == methods::PUT) && path_str == "/api/upload/multipart/chunk") {
        handleMultipartUploadChunk(request);
    }
    else if (method == methods::POST && path_str == "/api/upload/multipart/complete") {
        handleMultipartComplete(request);
    }
    else if (method == methods::POST && path_str == "/api/upload/multipart/cleanup") {
        handleMultipartCleanup(request);
    }
    else if (method == methods::GET && path_str.find("/api/file/") == 0 && path_str.find("/presign") != std::string::npos) {
        handleGetPresignUrlRoute(request);
    }
    else {
        LOG_WARN("Unhandled route: " + method_str + " " + path_str);
        replyErrorWithCors(request, status_codes::NotFound, U("Not Found"));
    }
}

void HttpServer::handleRegister(http_request request) {
    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("account")) || !json.has_field(U("password"))) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing account or password"));
            return;
        }
        std::string account = utility::conversions::to_utf8string(json.at(U("account")).as_string());
        std::string password = utility::conversions::to_utf8string(json.at(U("password")).as_string());

        auto respJson = ::handleRegister(account, password);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleLogin(http_request request) {
    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("account")) || !json.has_field(U("password"))) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing fields"));
            return;
        }
        std::string account = utility::conversions::to_utf8string(json.at(U("account")).as_string());
        std::string password = utility::conversions::to_utf8string(json.at(U("password")).as_string());

        auto respJson = ::handleLogin(account, password);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleUpload(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    std::string filename;
    auto query = request.request_uri().query();
    if (!query.empty()) {
        std::string q = utility::conversions::to_utf8string(query);
        size_t pos = q.find("filename=");
        if (pos != std::string::npos) {
            filename = q.substr(pos + 9);
            pos = filename.find('&');
            if (pos != std::string::npos) filename = filename.substr(0, pos);
            // 使用 Utils 中的 urlDecode 函数
            filename = urlDecode(filename);
        }
    }
    if (filename.empty()) {
        replyErrorWithCors(request, status_codes::BadRequest, U("Missing filename parameter"));
        return;
    }

    std::string content_type;
    auto ct = request.headers().content_type();
    if (!ct.empty()) {
        content_type = utility::conversions::to_utf8string(ct);
    }

    request.extract_vector().then([=](std::vector<unsigned char> data) {
        // 直接传 unsigned char 向量，避免拷贝到 vector<char>
        auto respJson = ::handleUpload(user->user_id, filename, data, content_type);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handlePresignUpload(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("filename")) || !json.has_field(U("size"))) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing filename or size"));
            return;
        }
        std::string filename = utility::conversions::to_utf8string(json.at(U("filename")).as_string());
        std::string content_type = json.has_field(U("content_type")) ?
            utility::conversions::to_utf8string(json.at(U("content_type")).as_string()) : "";
        size_t file_size = static_cast<size_t>(json.at(U("size")).as_number().to_uint64());

        auto respJson = ::handleRequestUploadUrl(user->user_id, filename, content_type, file_size);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleConfirmUploadRoute(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("file_id")) || !json.has_field(U("filename"))) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing file_id or filename"));
            return;
        }
        std::string file_id = utility::conversions::to_utf8string(json.at(U("file_id")).as_string());
        std::string filename = utility::conversions::to_utf8string(json.at(U("filename")).as_string());
        std::string content_type = json.has_field(U("content_type")) ?
            utility::conversions::to_utf8string(json.at(U("content_type")).as_string()) : "";
        size_t file_size = json.has_field(U("size")) ?
            static_cast<size_t>(json.at(U("size")).as_number().to_uint64()) : 0;

        auto respJson = ::handleConfirmUpload(user->user_id, file_id, filename, content_type, file_size);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleListFiles(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    int offset = 0, limit = 20;
    std::string search_keyword;
    auto query = request.request_uri().query();
    if (!query.empty()) {
        std::string q = utility::conversions::to_utf8string(query);

        // 解析offset
        size_t pos = q.find("offset=");
        if (pos != std::string::npos) {
            size_t start = pos + 7;
            size_t end = q.find("&", start);
            std::string offsetStr = (end != std::string::npos) ? q.substr(start, end - start) : q.substr(start);
            try {
                offset = std::stoi(offsetStr);
            } catch (...) {
                offset = 0;
            }
        }

        // 解析limit
        pos = q.find("limit=");
        if (pos != std::string::npos) {
            size_t start = pos + 6;
            size_t end = q.find("&", start);
            std::string limitStr = (end != std::string::npos) ? q.substr(start, end - start) : q.substr(start);
            try {
                limit = std::stoi(limitStr);
            } catch (...) {
                limit = 20;
            }
        }

        // 解析search
        pos = q.find("search=");
        if (pos != std::string::npos) {
            size_t start = pos + 7;
            size_t end = q.find("&", start);
            search_keyword = (end != std::string::npos) ? q.substr(start, end - start) : q.substr(start);
            // URL解码搜索关键词
            search_keyword = urlDecode(search_keyword);
        }
    }

    try {
        auto respJson = ::handleListFiles(user->user_id, offset, limit, search_keyword);
        replyJsonWithCors(request, status_codes::OK, respJson);
        LOG_INFO("handleListFiles: user=" + std::to_string(user->user_id) + " offset=" + std::to_string(offset) + " limit=" + std::to_string(limit) + " search='" + search_keyword + "'");
    } catch (const std::exception& e) {
        LOG_ERROR("handleListFiles exception: " + std::string(e.what()));
        replyErrorWithCors(request, status_codes::InternalError, U("Internal server error"));
    }
}

void HttpServer::handleDeleteFile(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    auto path = request.request_uri().path();
    std::string path_str = utility::conversions::to_utf8string(path);
    const std::string prefix = "/api/file/";
    if (path_str.length() <= prefix.length()) {
        replyErrorWithCors(request, status_codes::BadRequest, U("Invalid file ID"));
        return;
    }
    std::string file_id = path_str.substr(prefix.length());

    auto respJson = ::handleDeleteFile(user->user_id, file_id);
    replyJsonWithCors(request, status_codes::OK, respJson);
}

void HttpServer::handleBatchDeleteFiles(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("file_ids")) || !json[U("file_ids")].is_array()) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing file_ids array"));
            return;
        }

        std::vector<std::string> file_ids;
        auto file_ids_json = json[U("file_ids")].as_array();
        for (const auto& id : file_ids_json) {
            file_ids.push_back(utility::conversions::to_utf8string(id.as_string()));
        }

        if (file_ids.empty()) {
            replyErrorWithCors(request, status_codes::BadRequest, U("file_ids is empty"));
            return;
        }

        auto respJson = ::handleBatchDeleteFiles(user->user_id, file_ids);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleShare(http_request request) {
    auto path = request.request_uri().path();
    std::string path_str = utility::conversions::to_utf8string(path);
    const std::string prefix = "/api/share/";
    if (path_str.length() <= prefix.length()) {
        replyErrorWithCors(request, status_codes::BadRequest, U("Invalid file ID"));
        return;
    }
    std::string file_id = path_str.substr(prefix.length());

    auto respJson = ::handleShare(file_id);
    replyJsonWithCors(request, status_codes::OK, respJson);
}

void HttpServer::handleSetPublic(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    auto path = request.request_uri().path();
    std::string path_str = utility::conversions::to_utf8string(path);
    const std::string prefix = "/api/file/";
    size_t start = path_str.find(prefix);
    if (start == std::string::npos) {
        replyErrorWithCors(request, status_codes::BadRequest, U("Invalid path"));
        return;
    }
    std::string remaining = path_str.substr(start + prefix.length());
    size_t slash_pos = remaining.find('/');
    std::string file_id = (slash_pos == std::string::npos) ? remaining : remaining.substr(0, slash_pos);
    if (file_id.empty()) {
        replyErrorWithCors(request, status_codes::BadRequest, U("Invalid file ID"));
        return;
    }

    auto respJson = ::handleSetPublic(user->user_id, file_id);
    replyJsonWithCors(request, status_codes::OK, respJson);
}

void HttpServer::handleGetFile(http_request request) {
    auto path = request.request_uri().path();
    std::string path_str = utility::conversions::to_utf8string(path);
    const std::string prefix = "/api/i/";
    if (path_str.length() <= prefix.length()) {
        replyErrorWithCors(request, status_codes::BadRequest, U("Invalid file ID"));
        return;
    }
    std::string file_id = path_str.substr(prefix.length());

    auto user = Auth::verify(request);
    int user_id = user ? user->user_id : 0;

    // 解析查询参数
    auto query = request.request_uri().query();
    std::string query_str = utility::conversions::to_utf8string(query);

    bool download = (query_str.find("download") != std::string::npos);

    // 解析缩略图参数
    int thumb_width = 0, thumb_height = 0;
    size_t w_pos = query_str.find("w=");
    size_t h_pos = query_str.find("h=");
    if (w_pos != std::string::npos) {
        size_t end = query_str.find('&', w_pos);
        std::string w_val = (end == std::string::npos) ?
            query_str.substr(w_pos + 2) : query_str.substr(w_pos + 2, end - w_pos - 2);
        try { thumb_width = std::stoi(w_val); } catch (...) {}
    }
    if (h_pos != std::string::npos) {
        size_t end = query_str.find('&', h_pos);
        std::string h_val = (end == std::string::npos) ?
            query_str.substr(h_pos + 2) : query_str.substr(h_pos + 2, end - h_pos - 2);
        try { thumb_height = std::stoi(h_val); } catch (...) {}
    }

    auto user_agent = request.headers().find(U("User-Agent"));
    std::string ua_str = (user_agent != request.headers().end()) ? utility::conversions::to_utf8string(user_agent->second) : "";

    // 获取文件元数据
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        replyErrorWithCors(request, status_codes::NotFound, U("File not found"));
        return;
    }
    // 缩略图请求允许匿名访问（file_id是UUID，不易被猜测）
    bool isThumbRequest = (thumb_width > 0 || thumb_height > 0);
    if (!isThumbRequest && !meta.is_public && meta.user_id != user_id) {
        replyErrorWithCors(request, status_codes::Forbidden, U("Access denied"));
        return;
    }

    // If-None-Match: 图片命中缓存直接返回 304，省去 MinIO 网络往返
    if (isImage(meta.mime_type) && !download && !isThumbRequest) {
        auto ifNoneMatch = request.headers().find(U("If-None-Match"));
        if (ifNoneMatch != request.headers().end()) {
            std::string etag = "W/\"" + file_id + "\"";
            if (utility::conversions::to_utf8string(ifNoneMatch->second) == etag) {
                http_response response(status_codes::NotModified);
                response.headers().add(U("Cache-Control"), U("public, max-age=3600"));
                response.set_body(U(""));
                request.reply(response);
                return;
            }
        }
    }

    // 使用完整的 RFC 3986 URL 编码文件名
    std::string encodedFilename = urlEncode(meta.filename);

    // 根据 MIME 类型决定是内联显示还是附件下载
    std::string disp;
    if (download || isAttachmentType(meta.mime_type) || !isImage(meta.mime_type)) {
        // 同时使用 filename 和 filename* 确保兼容性
        disp = "attachment; filename=\"" + meta.filename + "\"; filename*=UTF-8''" + encodedFilename;
    } else {
        disp = "inline; filename=\"" + meta.filename + "\"; filename*=UTF-8''" + encodedFilename;
    }

    // 如果请求缩略图且是图片，生成缩略图（带 MinIO 缓存）
    if ((thumb_width > 0 || thumb_height > 0) && isImage(meta.mime_type)) {
        int tw = thumb_width > 0 ? thumb_width : 200;
        int th = thumb_height > 0 ? thumb_height : 200;
        std::string thumbKey = "thumbs/" + file_id + "_" + std::to_string(tw) + "_" + std::to_string(th);

        // 1. 检查 MinIO 缓存
        std::vector<char> thumbData;
        if (MinIOClient::instance().getObject(thumbKey, thumbData)) {
            http_response response(status_codes::OK);
            response.headers().add(U("Content-Type"), U("image/jpeg"));
            response.headers().add(U("Cache-Control"), U("public, max-age=86400"));
            response.set_body(std::vector<unsigned char>(thumbData.begin(), thumbData.end()));
            request.reply(response);
            return;
        }

        // 2. 缓存未命中，从 MinIO 获取原图并生成缩略图
        std::vector<char> fileData;
        if (MinIOClient::instance().getObject(file_id, fileData)) {
            if (ImageProcessor::generateThumbnail(fileData, thumbData, tw, th)) {
                // 异步缓存缩略图到 MinIO，不阻塞响应
                std::string cacheKey = thumbKey;
                std::vector<char> cacheData = thumbData;
                AeroQueue::instance().post([cacheKey, cacheData]() {
                    MinIOClient::instance().putObject(cacheKey, cacheData, "image/jpeg");
                });

                http_response response(status_codes::OK);
                response.headers().add(U("Content-Type"), U("image/jpeg"));
                response.headers().add(U("Cache-Control"), U("public, max-age=86400"));
                response.set_body(std::vector<unsigned char>(thumbData.begin(), thumbData.end()));
                request.reply(response);
                return;
            }
        }
        // 缩略图生成失败，回退到原始文件
    }

    // 对于下载请求，使用MinIO的预签名URL，让浏览器直接下载
    if (download || isAttachmentType(meta.mime_type)) {
        // 使用RFC 3986编码文件名
        std::string encodedFilename = urlEncode(meta.filename);
        std::string disposition = "attachment; filename=\"" + encodedFilename + "\"; filename*=UTF-8''" + encodedFilename;
        std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600, disposition);

        if (presignUrl.empty()) {
            LOG_ERROR("Failed to generate presigned URL for file: " + file_id);
            replyErrorWithCors(request, status_codes::NotFound, U("File not found"));
            return;
        }

        LOG_INFO("Generated presigned URL for file: " + file_id);
        http_response response(status_codes::Found);
        response.headers().add(U("Location"), utility::conversions::to_string_t(presignUrl));
        response.headers().add(U("Access-Control-Allow-Origin"), utility::conversions::to_string_t(getCorsOrigin()));
        response.headers().add(U("Cache-Control"), U("private, max-age=0, must-revalidate"));
        response.set_body(U(""));
        request.reply(response);
        return;
    }

    // 所有文件通过 302 重定向到 MinIO 预签名 URL 提供
    // 核心优化：文件不再读入应用内存，由 MinIO 直接返回给客户端，彻底消除 OOM 风险
    {
        std::string presignUrl = MinIOClient::instance().presignGetUrl(file_id, 3600, disp);
        if (!presignUrl.empty()) {
            http_response response(status_codes::Found);
            response.headers().add(U("Location"), utility::conversions::to_string_t(presignUrl));
            response.headers().add(U("Access-Control-Allow-Origin"), utility::conversions::to_string_t(getCorsOrigin()));
            response.headers().add(U("Cache-Control"), U("public, max-age=3600"));
            response.set_body(U(""));
            request.reply(response);
            return;
        }
    }

    replyErrorWithCors(request, status_codes::InternalError, U("Failed to generate download URL"));
}

void HttpServer::handleRequestUploadUrl(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("filename")) || !json.has_field(U("size"))) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing filename or size"));
            return;
        }
        std::string filename = utility::conversions::to_utf8string(json.at(U("filename")).as_string());
        std::string content_type = json.has_field(U("content_type")) ?
            utility::conversions::to_utf8string(json.at(U("content_type")).as_string()) : "";
        size_t file_size = static_cast<size_t>(json.at(U("size")).as_number().to_uint64());

        auto respJson = ::handleRequestUploadUrl(user->user_id, filename, content_type, file_size);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleConfirmUpload(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("file_id")) || !json.has_field(U("filename")) || !json.has_field(U("size"))) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing required fields"));
            return;
        }
        std::string file_id = utility::conversions::to_utf8string(json.at(U("file_id")).as_string());
        std::string filename = utility::conversions::to_utf8string(json.at(U("filename")).as_string());
        std::string content_type = json.has_field(U("content_type")) ?
            utility::conversions::to_utf8string(json.at(U("content_type")).as_string()) : "";
        size_t file_size = static_cast<size_t>(json.at(U("size")).as_number().to_uint64());

        auto respJson = ::handleConfirmUpload(user->user_id, file_id, filename, content_type, file_size);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleCleanup(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    // 检查是否为管理员（可以后续扩展角色系统）
    if (user->user_id != 1) {  // 这里简单处理，只允许用户ID为1的管理员
        replyErrorWithCors(request, status_codes::Forbidden, U("Forbidden"));
        return;
    }

    // 执行清理
    int cleaned = FileMetaDAO::instance().cleanupOrphanedFiles();

    http_response response(status_codes::OK);
    response.headers().add(U("Access-Control-Allow-Origin"), utility::conversions::to_string_t(getCorsOrigin()));
    response.headers().add(U("Content-Type"), U("application/json"));

    json::value respJson;
    respJson["status"] = json::value::string("success");
    respJson["cleaned_count"] = json::value::number(cleaned);
    respJson["message"] = json::value::string("Cleanup completed");

    response.set_body(respJson);
    request.reply(response);
}

void HttpServer::handleStats(http_request request) {
    // 统计接口需要登录认证
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }
    auto respJson = ::handleStats();
    replyJsonWithCors(request, status_codes::OK, respJson);
}

void HttpServer::handleSendVerificationCode(http_request request) {
    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("email"))) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing email"));
            return;
        }
        std::string email = utility::conversions::to_utf8string(json.at(U("email")).as_string());

        auto respJson = ::handleSendVerificationCode(email);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleEmailRegister(http_request request) {
    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("account")) || !json.has_field(U("password")) ||
            !json.has_field(U("email")) || !json.has_field(U("code"))) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing required fields"));
            return;
        }
        std::string account = utility::conversions::to_utf8string(json.at(U("account")).as_string());
        std::string password = utility::conversions::to_utf8string(json.at(U("password")).as_string());
        std::string email = utility::conversions::to_utf8string(json.at(U("email")).as_string());
        std::string code = utility::conversions::to_utf8string(json.at(U("code")).as_string());

        auto respJson = ::handleEmailRegister(account, password, email, code);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleMultipartInit(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("filename")) || !json.has_field(U("size"))) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing filename or size"));
            return;
        }
        std::string filename = utility::conversions::to_utf8string(json.at(U("filename")).as_string());
        std::string content_type = json.has_field(U("content_type")) ?
            utility::conversions::to_utf8string(json.at(U("content_type")).as_string()) : "";
        size_t file_size = static_cast<size_t>(json.at(U("size")).as_number().to_uint64());

        auto respJson = ::handleMultipartInit(user->user_id, filename, content_type, file_size);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleMultipartUploadChunk(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    // 从查询参数获取 upload_id 和 part_number
    auto query = request.request_uri().query();
    std::string q = utility::conversions::to_utf8string(query);
    std::string upload_id;
    int part_number = 0;

    size_t pos = q.find("upload_id=");
    if (pos != std::string::npos) {
        size_t start = pos + 10;
        size_t end = q.find("&", start);
        upload_id = (end != std::string::npos) ? q.substr(start, end - start) : q.substr(start);
    }
    pos = q.find("part_number=");
    if (pos != std::string::npos) {
        size_t start = pos + 12;
        size_t end = q.find("&", start);
        std::string val = (end != std::string::npos) ? q.substr(start, end - start) : q.substr(start);
        try { part_number = std::stoi(val); } catch (...) {}
    }

    if (upload_id.empty()) {
        replyErrorWithCors(request, status_codes::BadRequest, U("Missing upload_id"));
        return;
    }

    request.extract_vector().then([=](std::vector<unsigned char> data) {
        // 直接传 unsigned char，避免拷贝
        auto respJson = ::handleMultipartUploadChunk(upload_id, part_number, data);
        // 检查是否真的成功，失败返回 500 让前端感知
        auto status = respJson.has_field(U("error")) ? status_codes::InternalError : status_codes::OK;
        replyJsonWithCors(request, status, respJson);
    });
}

void HttpServer::handleMultipartComplete(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("upload_id")) || !json.has_field(U("filename")) || !json.has_field(U("size")) || !json.has_field(U("total_chunks"))) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing required fields"));
            return;
        }
        std::string upload_id = utility::conversions::to_utf8string(json.at(U("upload_id")).as_string());
        std::string filename = utility::conversions::to_utf8string(json.at(U("filename")).as_string());
        std::string content_type = json.has_field(U("content_type")) ?
            utility::conversions::to_utf8string(json.at(U("content_type")).as_string()) : "";
        size_t file_size = static_cast<size_t>(json.at(U("size")).as_number().to_uint64());
        int total_chunks = json.at(U("total_chunks")).as_number().to_int32();

        auto respJson = ::handleMultipartComplete(user->user_id, upload_id, filename, content_type, file_size, total_chunks);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleMultipartCleanup(http_request request) {
    auto user = Auth::verify(request);
    if (!user) {
        replyErrorWithCors(request, status_codes::Unauthorized, U("Unauthorized"));
        return;
    }

    request.extract_json().then([=](json::value json) {
        if (!json.has_field(U("upload_id"))) {
            replyErrorWithCors(request, status_codes::BadRequest, U("Missing upload_id"));
            return;
        }
        std::string upload_id = utility::conversions::to_utf8string(json.at(U("upload_id")).as_string());

        auto respJson = ::handleMultipartCleanup(user->user_id, upload_id);
        replyJsonWithCors(request, status_codes::OK, respJson);
    });
}

void HttpServer::handleGetPresignUrlRoute(http_request request) {
    auto user = Auth::verify(request);
    int user_id = user ? user->user_id : 0;

    auto path = request.request_uri().path();
    std::string path_str = utility::conversions::to_utf8string(path);
    std::string prefix = "/api/file/";
    size_t start = path_str.find(prefix);
    if (start == std::string::npos) {
        replyErrorWithCors(request, status_codes::BadRequest, U("Invalid path"));
        return;
    }
    std::string remaining = path_str.substr(start + prefix.length());
    size_t slash_pos = remaining.find('/');
    std::string file_id = (slash_pos == std::string::npos) ? remaining : remaining.substr(0, slash_pos);

    auto respJson = ::handleGetPresignUrl(user_id, file_id);
    replyJsonWithCors(request, status_codes::OK, respJson);
}
