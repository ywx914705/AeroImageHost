#pragma once

#include <cpprest/http_listener.h>
#include <memory>
#include <string>

class HttpServer {
public:
    HttpServer(int port);
    void start();

private:
    int port_;
    std::unique_ptr<web::http::experimental::listener::http_listener> listener_;

    void setupRoutes();
    void handleOptions(web::http::http_request request);
    void handleAll(web::http::http_request request);
    void handleRegister(web::http::http_request request);
    void handleLogin(web::http::http_request request);
    void handleUpload(web::http::http_request request);
    void handleListFiles(web::http::http_request request);
    void handleDeleteFile(web::http::http_request request);
    void handleBatchDeleteFiles(web::http::http_request request);
    void handleGetFile(web::http::http_request request);
    void handleShare(web::http::http_request request);
    void handleSetPublic(web::http::http_request request);  // 新增
    void handleRequestUploadUrl(web::http::http_request request);
    void handleConfirmUpload(web::http::http_request request);
    void handleCleanup(web::http::http_request request);
    void handleStats(web::http::http_request request);
    // 邮箱注册相关
    void handleSendVerificationCode(web::http::http_request request);
    void handleEmailRegister(web::http::http_request request);
    // 分片上传
    void handleMultipartInit(web::http::http_request request);
    void handleMultipartComplete(web::http::http_request request);
    void handleMultipartCleanup(web::http::http_request request);
    void handleGetPresignUrlRoute(web::http::http_request request);
};