/*
 * UploadController.h - 上传路由 Controller
 *
 * 路由：
 *   POST /api/upload                     - 直接上传文件
 *   POST /api/upload/presign             - 申请预签名上传 URL
 *   POST /api/upload/confirm             - 确认预签名上传完成
 *   POST /api/upload/multipart/init      - 初始化分片上传
 *   POST /api/upload/multipart/chunk     - 上传单个分片
 *   POST /api/upload/multipart/complete  - 完成分片上传并合并
 *   POST /api/upload/multipart/cleanup   - 清理分片上传临时数据
 */
#ifndef UPLOAD_CONTROLLER_H
#define UPLOAD_CONTROLLER_H

#include <drogon/HttpController.h>

using namespace drogon;

class UploadController : public drogon::HttpController<UploadController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UploadController::upload, "/api/upload", Post);
    ADD_METHOD_TO(UploadController::presign, "/api/upload/presign", Post);
    ADD_METHOD_TO(UploadController::confirm, "/api/upload/confirm", Post);
    ADD_METHOD_TO(UploadController::multipartInit, "/api/upload/multipart/init", Post);
    ADD_METHOD_TO(UploadController::multipartChunk, "/api/upload/multipart/chunk", Post);
    ADD_METHOD_TO(UploadController::multipartComplete, "/api/upload/multipart/complete", Post);
    ADD_METHOD_TO(UploadController::multipartCleanup, "/api/upload/multipart/cleanup", Post);
    METHOD_LIST_END

    void upload(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void presign(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void confirm(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void multipartInit(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void multipartChunk(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void multipartComplete(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void multipartCleanup(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
};

#endif // UPLOAD_CONTROLLER_H
