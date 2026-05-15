/*
 * SystemController.h - 系统路由 Controller
 *
 * 路由：
 *   GET  /api/health  - 深度健康检查（MySQL / Redis / MinIO）
 *   GET  /api/stats   - 系统统计信息（需登录）
 *   GET  /api/metrics - 指标 JSON（需登录）
 *   POST /api/cleanup - 触发孤儿分片清理
 */
#ifndef SYSTEM_CONTROLLER_H
#define SYSTEM_CONTROLLER_H

#include <drogon/HttpController.h>

using namespace drogon;

class SystemController : public drogon::HttpController<SystemController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SystemController::health, "/api/health", Get);
    ADD_METHOD_TO(SystemController::stats, "/api/stats", Get);
    ADD_METHOD_TO(SystemController::metrics, "/api/metrics", Get);
    ADD_METHOD_TO(SystemController::cleanup, "/api/cleanup", Post);
    METHOD_LIST_END

    void health(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void stats(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void metrics(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void cleanup(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
};

#endif // SYSTEM_CONTROLLER_H
