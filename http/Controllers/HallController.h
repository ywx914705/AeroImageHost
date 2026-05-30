#ifndef HALL_CONTROLLER_H
#define HALL_CONTROLLER_H

#include <drogon/HttpController.h>

using namespace drogon;

class HallController : public drogon::HttpController<HallController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(HallController::listPosts, "/api/hall", Get);
        ADD_METHOD_TO(HallController::publish, "/api/hall/publish", Post);
        ADD_METHOD_TO(HallController::deletePost, "/api/hall/{id}/delete", Delete);
        ADD_METHOD_TO(HallController::toggleLike, "/api/hall/{id}/like", Post);
        ADD_METHOD_TO(HallController::getTags, "/api/hall/tags", Get);
    METHOD_LIST_END

    void listPosts(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void publish(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void deletePost(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& postId);
    void toggleLike(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& postId);
    void getTags(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
};

#endif
