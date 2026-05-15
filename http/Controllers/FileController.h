/*
 * FileController.h - 文件管理路由 Controller
 *
 * 路由：
 *   GET    /api/files?offset&limit&search  - 获取文件列表
 *   DELETE /api/file/{1}                   - 删除指定文件
 *   POST   /api/files/batch-delete         - 批量删除文件
 *   PUT    /api/file/{1}/public            - 切换文件公开/私有状态
 *   GET    /api/file/{1}/presign           - 获取文件预签名下载 URL
 *   GET    /api/share/{1}                  - 获取文件分享信息
 */
#ifndef FILE_CONTROLLER_H
#define FILE_CONTROLLER_H

#include <drogon/HttpController.h>

using namespace drogon;

class FileController : public drogon::HttpController<FileController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FileController::listFiles, "/api/files", Get);
    ADD_METHOD_TO(FileController::deleteFile, "/api/file/{1}", Delete);
    ADD_METHOD_TO(FileController::batchDelete, "/api/files/batch-delete", Post);
    ADD_METHOD_TO(FileController::setPublic, "/api/file/{1}/public", Put);
    ADD_METHOD_TO(FileController::getPresign, "/api/file/{1}/presign", Get);
    ADD_METHOD_TO(FileController::share, "/api/share/{1}", Get);
    METHOD_LIST_END

    void listFiles(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void deleteFile(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId);
    void batchDelete(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void setPublic(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId);
    void getPresign(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId);
    void share(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId);
};

#endif // FILE_CONTROLLER_H
