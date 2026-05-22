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
    ADD_METHOD_TO(FileController::addWatermark, "/api/file/{1}/wm/add", Post);
    ADD_METHOD_TO(FileController::removeWatermark, "/api/file/{1}/wm/remove", Post);
    ADD_METHOD_TO(FileController::getWatermark, "/api/file/{1}/wm/config", Get);
    METHOD_LIST_END

    void listFiles(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void deleteFile(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId);
    void batchDelete(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void setPublic(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId);
    void getPresign(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId);
    void share(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId);
    void addWatermark(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId);
    void removeWatermark(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId);
    void getWatermark(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId);
};

#endif
