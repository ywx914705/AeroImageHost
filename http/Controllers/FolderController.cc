#include "FolderController.h"
#include "ControllerHelpers.hpp"
#include "FolderDAO.hpp"
#include "Log.hpp"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

using namespace rapidjson;

// 最大嵌套深度
static const int MAX_FOLDER_DEPTH = 5;

// 将 Folder 结构体转为 RapidJSON 对象
static void folderToJson(const Folder& f, Value& obj, Document& doc) {
    obj.SetObject();
    auto& alloc = doc.GetAllocator();
    obj.AddMember("id", static_cast<int64_t>(f.id), alloc);
    obj.AddMember("user_id", f.user_id, alloc);
    obj.AddMember("parent_id", static_cast<int64_t>(f.parent_id), alloc);
    obj.AddMember("name", StringRef(f.name.c_str()), alloc);
    obj.AddMember("icon", StringRef(f.icon.c_str()), alloc);
    obj.AddMember("color", StringRef(f.color.c_str()), alloc);
    obj.AddMember("sort_order", f.sort_order, alloc);
    obj.AddMember("created_at", static_cast<int64_t>(f.created_at), alloc);
    obj.AddMember("updated_at", static_cast<int64_t>(f.updated_at), alloc);
    obj.AddMember("file_count", f.file_count, alloc);
    obj.AddMember("child_count", f.child_count, alloc);
}

// 将 Folder 数组转为 RapidJSON 数组
static Value foldersToJson(const std::vector<Folder>& folders, Document& doc) {
    Value arr(kArrayType);
    auto& alloc = doc.GetAllocator();
    for (const auto& f : folders) {
        Value obj(kObjectType);
        folderToJson(f, obj, doc);
        arr.PushBack(obj, alloc);
    }
    return arr;
}

// GET /api/folders?parent_id=0
void FolderController::listFolders(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    long long parent_id = 0;
    auto& params = req->getParameters();
    auto it = params.find("parent_id");
    if (it != params.end()) {
        try { parent_id = std::stoll(it->second); } catch (...) {}
    }

    auto folders = FolderDAO::instance().listByUser(user_id, parent_id);

    Document resp;
    resp.SetObject();
    resp.AddMember("success", true, resp.GetAllocator());
    resp.AddMember("data", foldersToJson(folders, resp), resp.GetAllocator());

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    resp.Accept(writer);
    respond(HandlerResult::ok(buffer.GetString()), std::move(callback));
}

// POST /api/folders
void FolderController::createFolder(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    std::string name = json.get("name", "").asString();
    long long parent_id = json.get("parent_id", 0).asInt64();

    // 验证文件夹名
    if (name.empty()) {
        respond(HandlerResult::error(errorResponse("Folder name is required"), 400), std::move(callback));
        return;
    }
    if (name.size() > 255) {
        respond(HandlerResult::error(errorResponse("Folder name too long"), 400), std::move(callback));
        return;
    }

    // 检查嵌套深度
    if (parent_id > 0) {
        int depth = FolderDAO::instance().getDepth(parent_id, user_id) + 1;
        if (depth >= MAX_FOLDER_DEPTH) {
            respond(HandlerResult::error(errorResponse("Folder nesting exceeds maximum depth"), 400), std::move(callback));
            return;
        }
    }

    long long new_id = FolderDAO::instance().create(user_id, parent_id, name);
    if (new_id < 0) {
        respond(HandlerResult::error(errorResponse("Failed to create folder"), 500), std::move(callback));
        return;
    }

    Folder folder = FolderDAO::instance().get(new_id, user_id);

    Document resp;
    resp.SetObject();
    resp.AddMember("success", true, resp.GetAllocator());
    Value folderObj(kObjectType);
    folderToJson(folder, folderObj, resp);
    resp.AddMember("data", folderObj, resp.GetAllocator());

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    resp.Accept(writer);
    respond(HandlerResult::created(buffer.GetString()), std::move(callback));
}

// GET /api/folders/{id}
void FolderController::getFolder(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& folderId) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    long long fid = 0;
    try { fid = std::stoll(folderId); } catch (...) {
        respond(HandlerResult::error(errorResponse("Invalid folder ID"), 400), std::move(callback));
        return;
    }

    Folder folder = FolderDAO::instance().get(fid, user_id);
    if (folder.id == 0) {
        respond(HandlerResult::error(errorResponse("Folder not found"), 404), std::move(callback));
        return;
    }

    // 获取子文件夹和路径
    auto children = FolderDAO::instance().getChildren(fid, user_id);
    auto path = FolderDAO::instance().getPath(fid, user_id);

    Document resp;
    resp.SetObject();
    resp.AddMember("success", true, resp.GetAllocator());

    Value folderObj(kObjectType);
    folderToJson(folder, folderObj, resp);
    resp.AddMember("data", folderObj, resp.GetAllocator());

    resp.AddMember("children", foldersToJson(children, resp), resp.GetAllocator());

    Value pathArr(kArrayType);
    auto& alloc = resp.GetAllocator();
    for (const auto& p : path) {
        Value pObj(kObjectType);
        pObj.AddMember("id", static_cast<int64_t>(p.id), alloc);
        pObj.AddMember("name", StringRef(p.name.c_str()), alloc);
        pathArr.PushBack(pObj, alloc);
    }
    resp.AddMember("path", pathArr, alloc);

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    resp.Accept(writer);
    respond(HandlerResult::ok(buffer.GetString()), std::move(callback));
}

// PUT /api/folders/{id}
void FolderController::updateFolder(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& folderId) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    long long fid = 0;
    try { fid = std::stoll(folderId); } catch (...) {
        respond(HandlerResult::error(errorResponse("Invalid folder ID"), 400), std::move(callback));
        return;
    }

    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    Folder folder = FolderDAO::instance().get(fid, user_id);
    if (folder.id == 0) {
        respond(HandlerResult::error(errorResponse("Folder not found"), 404), std::move(callback));
        return;
    }

    bool updated = false;

    // 重命名
    if (json.isMember("name")) {
        std::string newName = json.get("name", "").asString();
        if (newName.empty()) {
            respond(HandlerResult::error(errorResponse("Folder name cannot be empty"), 400), std::move(callback));
            return;
        }
        if (newName.size() > 255) {
            respond(HandlerResult::error(errorResponse("Folder name too long"), 400), std::move(callback));
            return;
        }
        if (!FolderDAO::instance().rename(fid, user_id, newName)) {
            respond(HandlerResult::error(errorResponse("Failed to rename folder"), 500), std::move(callback));
            return;
        }
        updated = true;
    }

    // 更新图标和颜色
    if (json.isMember("icon") || json.isMember("color")) {
        std::string icon = json.isMember("icon") ? json.get("icon", folder.icon).asString() : folder.icon;
        std::string color = json.isMember("color") ? json.get("color", folder.color).asString() : folder.color;
        if (!FolderDAO::instance().updateAppearance(fid, user_id, icon, color)) {
            respond(HandlerResult::error(errorResponse("Failed to update folder appearance"), 500), std::move(callback));
            return;
        }
        updated = true;
    }

    // 更新排序
    if (json.isMember("sort_order")) {
        int sortOrder = json.get("sort_order", 0).asInt();
        if (!FolderDAO::instance().updateSort(fid, user_id, sortOrder)) {
            respond(HandlerResult::error(errorResponse("Failed to update folder sort order"), 500), std::move(callback));
            return;
        }
        updated = true;
    }

    if (!updated) {
        respond(HandlerResult::error(errorResponse("No fields to update"), 400), std::move(callback));
        return;
    }

    // 返回更新后的文件夹
    Folder updatedFolder = FolderDAO::instance().get(fid, user_id);

    Document resp;
    resp.SetObject();
    resp.AddMember("success", true, resp.GetAllocator());
    Value folderObj(kObjectType);
    folderToJson(updatedFolder, folderObj, resp);
    resp.AddMember("data", folderObj, resp.GetAllocator());

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    resp.Accept(writer);
    respond(HandlerResult::ok(buffer.GetString()), std::move(callback));
}

// DELETE /api/folders/{id}
void FolderController::deleteFolder(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& folderId) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    long long fid = 0;
    try { fid = std::stoll(folderId); } catch (...) {
        respond(HandlerResult::error(errorResponse("Invalid folder ID"), 400), std::move(callback));
        return;
    }

    Folder folder = FolderDAO::instance().get(fid, user_id);
    if (folder.id == 0) {
        respond(HandlerResult::error(errorResponse("Folder not found"), 404), std::move(callback));
        return;
    }

    if (!FolderDAO::instance().deleteFolder(fid, user_id)) {
        respond(HandlerResult::error(errorResponse("Failed to delete folder"), 500), std::move(callback));
        return;
    }

    Document resp;
    resp.SetObject();
    resp.AddMember("success", true, resp.GetAllocator());
    resp.AddMember("message", "Folder deleted", resp.GetAllocator());

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    resp.Accept(writer);
    respond(HandlerResult::ok(buffer.GetString()), std::move(callback));
}

// PUT /api/folders/{id}/move
void FolderController::moveFolder(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& folderId) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    long long fid = 0;
    try { fid = std::stoll(folderId); } catch (...) {
        respond(HandlerResult::error(errorResponse("Invalid folder ID"), 400), std::move(callback));
        return;
    }

    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    long long newParentId = json.get("parent_id", 0).asInt64();

    Folder folder = FolderDAO::instance().get(fid, user_id);
    if (folder.id == 0) {
        respond(HandlerResult::error(errorResponse("Folder not found"), 404), std::move(callback));
        return;
    }

    // 不能移动到自身
    if (fid == newParentId) {
        respond(HandlerResult::error(errorResponse("Cannot move folder to itself"), 400), std::move(callback));
        return;
    }

    // 检查循环引用
    if (newParentId > 0 && FolderDAO::instance().isDescendant(fid, newParentId, user_id)) {
        respond(HandlerResult::error(errorResponse("Cannot move folder to a descendant"), 400), std::move(callback));
        return;
    }

    // 检查嵌套深度
    if (newParentId > 0) {
        int depth = FolderDAO::instance().getDepth(newParentId, user_id) + 1;
        if (depth >= MAX_FOLDER_DEPTH) {
            respond(HandlerResult::error(errorResponse("Moving folder would exceed maximum nesting depth"), 400), std::move(callback));
            return;
        }
    }

    if (!FolderDAO::instance().move(fid, user_id, newParentId)) {
        respond(HandlerResult::error(errorResponse("Failed to move folder"), 500), std::move(callback));
        return;
    }

    Folder movedFolder = FolderDAO::instance().get(fid, user_id);

    Document resp;
    resp.SetObject();
    resp.AddMember("success", true, resp.GetAllocator());
    Value folderObj(kObjectType);
    folderToJson(movedFolder, folderObj, resp);
    resp.AddMember("data", folderObj, resp.GetAllocator());

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    resp.Accept(writer);
    respond(HandlerResult::ok(buffer.GetString()), std::move(callback));
}
