#ifndef FOLDER_CONTROLLER_H
#define FOLDER_CONTROLLER_H

#include <drogon/HttpController.h>

using namespace drogon;

class FolderController : public drogon::HttpController<FolderController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(FolderController::listFolders, "/api/folders", Get);
        ADD_METHOD_TO(FolderController::createFolder, "/api/folders", Post);
        ADD_METHOD_TO(FolderController::getFolder, "/api/folders/{1}", Get);
        ADD_METHOD_TO(FolderController::updateFolder, "/api/folders/{1}", Put);
        ADD_METHOD_TO(FolderController::deleteFolder, "/api/folders/{1}", Delete);
        ADD_METHOD_TO(FolderController::moveFolder, "/api/folders/{1}/move", Put);
    METHOD_LIST_END

    void listFolders(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void createFolder(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void getFolder(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& folderId);
    void updateFolder(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& folderId);
    void deleteFolder(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& folderId);
    void moveFolder(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& folderId);
};

#endif
