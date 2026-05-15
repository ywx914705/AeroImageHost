#include "FileController.h"
#include "Handlers.hpp"
#include "ControllerHelpers.hpp"
#include "Config.hpp"
#include <json/json.h>

void FileController::listFiles(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    auto& params = req->getParameters();
    int offset = 0, limit = 20;
    std::string search;
    auto it = params.find("offset");
    if (it != params.end()) { try { offset = std::stoi(it->second); } catch (...) {} }
    it = params.find("limit");
    if (it != params.end()) { try { limit = std::stoi(it->second); } catch (...) {} }
    it = params.find("search");
    if (it != params.end()) { search = it->second; }

    const int max_page = Config::instance().getInt("files_list.max_page_size", 100);
    const int max_offset = Config::instance().getInt("files_list.max_offset", 5000000);
    if (offset < 0) offset = 0;
    if (offset > max_offset) offset = max_offset;
    if (limit < 1) limit = 1;
    if (limit > max_page) limit = max_page;

    respond(handleListFiles(user_id, offset, limit, search), std::move(callback));
}

void FileController::deleteFile(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    respond(handleDeleteFile(user_id, fileId), std::move(callback));
}

void FileController::batchDelete(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    std::vector<std::string> file_ids;
    const auto& arr = json.get("file_ids", Json::Value(Json::arrayValue));
    if (arr.isArray()) {
        for (const auto& item : arr) {
            if (item.isString()) file_ids.push_back(item.asString());
        }
    }

    respond(handleBatchDeleteFiles(user_id, file_ids), std::move(callback));
}

void FileController::setPublic(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    auto json = req->getJsonObject();
    if (json && json->isMember("is_public")) {
        respond(handleSetPublic(user_id, fileId, json->get("is_public", false).asBool()), std::move(callback));
        return;
    }
    respond(handleSetPublic(user_id, fileId), std::move(callback));
}

void FileController::getPresign(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    respond(handleGetPresignUrl(user_id, fileId), std::move(callback));
}

void FileController::share(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& fileId) {
    respond(handleShare(fileId), std::move(callback));
}
