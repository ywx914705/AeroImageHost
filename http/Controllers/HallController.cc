/*
 * HallController.cc - 图床大厅控制器实现
 *
 * 职责：处理大厅相关的 HTTP 请求（列表、发布、删除、点赞、标签统计）。
 *       使用 requireAuth 进行身份验证，使用 HandlerResult 统一响应格式。
 */
#include "HallController.h"
#include "ControllerHelpers.hpp"
#include "HallDAO.hpp"
#include "FileMeta.hpp"
#include "Utils.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include <json/json.h>

// ===================== listPosts =====================
void HallController::listPosts(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    auto& params = req->getParameters();
    int offset = 0, limit = 20;
    std::string sort = "latest", tag;

    auto it = params.find("offset");
    if (it != params.end()) { try { offset = std::stoi(it->second); } catch (...) {} }
    it = params.find("limit");
    if (it != params.end()) { try { limit = std::stoi(it->second); } catch (...) {} }
    it = params.find("sort");
    if (it != params.end()) { sort = it->second; }
    it = params.find("tag");
    if (it != params.end()) { tag = it->second; }

    // 参数校验
    if (offset < 0) offset = 0;
    if (limit < 1) limit = 1;
    if (limit > 100) limit = 100;
    // 白名单校验 sort
    if (sort != "latest" && sort != "popular" && sort != "views") {
        sort = "latest";
    }

    // 获取当前用户 ID（可选，用于判断是否已点赞）
    int viewer_user_id = getUserIdFromToken(req);

    auto [posts, total] = HallDAO::instance().listPosts(offset, limit, sort, tag, viewer_user_id);

    static const std::string publicUrl = Config::instance().getString("minio.public_url");

    Json::Value resp;
    resp["total"] = total;
    Json::Value postsArr(Json::arrayValue);
    for (const auto& p : posts) {
        Json::Value obj;
        obj["id"] = static_cast<Json::Int64>(p.id);
        obj["file_id"] = p.file_id;
        obj["user_id"] = p.user_id;
        obj["title"] = p.title;
        obj["description"] = p.description;
        obj["tags"] = p.tags;
        obj["likes"] = p.likes;
        obj["views"] = p.views;
        obj["created_at"] = static_cast<Json::Int64>(p.created_at);
        obj["filename"] = p.filename;
        obj["mime_type"] = p.mime_type;
        obj["file_size"] = static_cast<Json::Int64>(p.file_size);
        obj["username"] = p.username;
        obj["is_liked"] = p.is_liked;
        obj["image_url"] = publicUrl + p.file_id;
        postsArr.append(obj);
    }
    resp["posts"] = postsArr;

    Json::FastWriter writer;
    respond(HandlerResult::ok(writer.write(resp)), std::move(callback));
}

// ===================== publish =====================
void HallController::publish(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    std::string file_id = json.get("file_id", "").asString();
    std::string title = json.get("title", "").asString();
    std::string description = json.get("description", "").asString();
    std::string tags = json.get("tags", "").asString();

    if (file_id.empty()) {
        respond(HandlerResult::error(errorResponse("file_id is required"), 400), std::move(callback));
        return;
    }
    if (title.empty()) {
        respond(HandlerResult::error(errorResponse("title is required"), 400), std::move(callback));
        return;
    }
    if (title.size() > 200) {
        respond(HandlerResult::error(errorResponse("title too long (max 200 characters)"), 400), std::move(callback));
        return;
    }
    if (description.size() > 2000) {
        respond(HandlerResult::error(errorResponse("description too long (max 2000 characters)"), 400), std::move(callback));
        return;
    }
    if (tags.size() > 500) {
        respond(HandlerResult::error(errorResponse("tags too long (max 500 characters)"), 400), std::move(callback));
        return;
    }

    // 验证文件属于当前用户且 is_public=true
    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        respond(HandlerResult::error(errorResponse("File not found"), 404), std::move(callback));
        return;
    }
    if (meta.user_id != user_id) {
        respond(HandlerResult::error(errorResponse("Permission denied"), 403), std::move(callback));
        return;
    }
    if (!meta.is_public) {
        respond(HandlerResult::error(errorResponse("File must be public to publish to hall"), 400), std::move(callback));
        return;
    }

    // 检查文件是否已经发布到大厅
    if (HallDAO::instance().isPublished(file_id)) {
        respond(HandlerResult::error(errorResponse("File already published to hall"), 409), std::move(callback));
        return;
    }

    HallPost post;
    post.file_id = file_id;
    post.user_id = user_id;
    post.title = title;
    post.description = description;
    post.tags = tags;

    if (!HallDAO::instance().publish(post)) {
        respond(HandlerResult::error(errorResponse("Failed to publish"), 500), std::move(callback));
        return;
    }

    Json::Value resp;
    resp["status"] = "success";
    Json::FastWriter writer;
    respond(HandlerResult::ok(writer.write(resp)), std::move(callback));
}

// ===================== deletePost =====================
void HallController::deletePost(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& postId) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    long long post_id = 0;
    try { post_id = std::stoll(postId); } catch (...) {
        respond(HandlerResult::error(errorResponse("Invalid post id"), 400), std::move(callback));
        return;
    }
    if (post_id <= 0) {
        respond(HandlerResult::error(errorResponse("Invalid post id"), 400), std::move(callback));
        return;
    }

    if (!HallDAO::instance().deletePost(post_id, user_id)) {
        respond(HandlerResult::error(errorResponse("Post not found or permission denied"), 404), std::move(callback));
        return;
    }

    Json::Value resp;
    resp["status"] = "success";
    Json::FastWriter writer;
    respond(HandlerResult::ok(writer.write(resp)), std::move(callback));
}

// ===================== toggleLike =====================
void HallController::toggleLike(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& postId) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    long long post_id = 0;
    try { post_id = std::stoll(postId); } catch (...) {
        respond(HandlerResult::error(errorResponse("Invalid post id"), 400), std::move(callback));
        return;
    }
    if (post_id <= 0) {
        respond(HandlerResult::error(errorResponse("Invalid post id"), 400), std::move(callback));
        return;
    }

    if (!HallDAO::instance().toggleLike(post_id, user_id)) {
        respond(HandlerResult::error(errorResponse("Failed to toggle like"), 500), std::move(callback));
        return;
    }

    // 返回更新后的帖子信息
    HallPost updatedPost = HallDAO::instance().getPost(post_id, user_id);
    Json::Value resp;
    resp["status"] = "success";
    resp["likes"] = updatedPost.likes;
    resp["is_liked"] = updatedPost.is_liked;
    Json::FastWriter writer;
    respond(HandlerResult::ok(writer.write(resp)), std::move(callback));
}

// ===================== getTags =====================
void HallController::getTags(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    auto tags = HallDAO::instance().getTags();

    Json::Value tagsArr(Json::arrayValue);
    for (const auto& [tag, count] : tags) {
        Json::Value tagObj;
        tagObj["name"] = tag;
        tagObj["count"] = count;
        tagsArr.append(tagObj);
    }

    Json::Value resp;
    resp["tags"] = tagsArr;

    Json::FastWriter writer;
    respond(HandlerResult::ok(writer.write(resp)), std::move(callback));
}
