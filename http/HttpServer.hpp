/*
 * HttpServer.hpp - HTTP 服务器头文件
 *
 * 职责：基于 cpprestsdk 的 HTTP 服务器，负责接收所有 /api/* 请求，
 *       根据路由分发到对应的 handler 方法，再委托给 Handlers 模块执行业务逻辑。
 *
 * 设计模式：单实例服务器，所有 handler 是私有成员方法。
 * 路由由 handleAll() 统一分发（未使用 cpprestsdk 的路由机制）。
 *
 * 在项目中的作用：HTTP 层入口，连接前端 Vue SPA 和后端业务逻辑。
 */
#pragma once

#include <cpprest/http_listener.h>
#include <memory>
#include <string>

class HttpServer {
public:
    HttpServer(int port); // 创建监听指定端口的 HTTP 服务器
    void start();         // 启动监听（阻塞直到监听成功）
    void stop();          // 停止监听，拒绝新连接

private:
    int port_;                                                    // 监听端口号
    std::unique_ptr<web::http::experimental::listener::http_listener> listener_; // cpprestsdk HTTP 监听器

    // 路由设置和分发
    void setupRoutes();                                           // 注册路由处理器
    void handleOptions(web::http::http_request request);          // 处理 CORS 预检请求
    void handleAll(web::http::http_request request);              // 统一路由分发入口

    // 用户认证相关
    void handleRegister(web::http::http_request request);         // POST /api/auth/register - 普通注册
    void handleLogin(web::http::http_request request);            // POST /api/auth/login - 登录

    // 文件上传相关
    void handleUpload(web::http::http_request request);           // POST /api/upload - 直接上传
    void handlePresignUpload(web::http::http_request request);    // POST /api/upload/presign - 预签名上传
    void handleConfirmUploadRoute(web::http::http_request request); // POST /api/upload/confirm - 确认上传

    // 分片上传相关
    void handleMultipartInit(web::http::http_request request);    // POST /api/upload/multipart/init
    void handleMultipartUploadChunk(web::http::http_request request); // POST /api/upload/multipart/chunk
    void handleMultipartComplete(web::http::http_request request);   // POST /api/upload/multipart/complete
    void handleMultipartCleanup(web::http::http_request request);    // POST /api/upload/multipart/cleanup

    // 文件管理相关
    void handleListFiles(web::http::http_request request);        // GET /api/files - 文件列表
    void handleDeleteFile(web::http::http_request request);       // DELETE /api/file/{id} - 删除文件
    void handleBatchDeleteFiles(web::http::http_request request); // POST /api/files/batch-delete - 批量删除
    void handleGetFile(web::http::http_request request);          // GET /api/i/{id} - 访问文件（含缩略图）
    void handleShare(web::http::http_request request);            // POST /api/share/{id} - 分享链接
    void handleSetPublic(web::http::http_request request);        // PUT /api/file/{id}/public - 切换公开/私有
    void handleGetPresignUrlRoute(web::http::http_request request); // GET /api/file/{id}/presign - 按需获取预签名URL

    // 系统管理相关
    void handleHealth(web::http::http_request request);           // GET /api/health - 健康检查
    void handleCleanup(web::http::http_request request);          // POST /api/cleanup - 清理孤儿文件
    void handleStats(web::http::http_request request);            // GET /api/stats - 系统统计

    // 邮箱注册相关
    void handleSendVerificationCode(web::http::http_request request); // POST /api/auth/send-code
    void handleEmailRegister(web::http::http_request request);        // POST /api/auth/register/email
};