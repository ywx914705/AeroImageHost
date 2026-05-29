#include "SystemController.h"
#include "Handlers.hpp"
#include "ControllerHelpers.hpp"
#include "Config.hpp"
#include "MetricsCollector.hpp"
#include "ConnectionPool.hpp"
#include "RedisClient.hpp"
#include "MinIOClient.hpp"
#include <json/json.h>
#include <mysql/mysql.h>
#include <drogon/drogon.h>
#include <csignal>
#include <unistd.h>

namespace {

Json::Value buildDeepHealth() {
    Json::Value root;
    bool all_ok = true;

    bool mysql_ok = false;
    MYSQL* conn = ConnectionPool::getInstance().getConnection();
    if (conn) {
        mysql_ok = (mysql_ping(conn) == 0);
        ConnectionPool::getInstance().releaseConnection(conn);
    }
    root["mysql"] = mysql_ok ? "ok" : "fail";
    all_ok = all_ok && mysql_ok;

    const std::string pong = RedisClient::instance().ping();
    const bool redis_ok = (pong == "PONG");
    root["redis"] = redis_ok ? "ok" : "fail";
    all_ok = all_ok && redis_ok;

    static bool cachedMinioOk = false;
    static std::chrono::steady_clock::time_point lastMinioCheck{};
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastMinioCheck).count() >= 5) {
        cachedMinioOk = MinIOClient::instance().isHealthy();
        lastMinioCheck = now;
    }
    root["minio"] = cachedMinioOk ? "ok" : "fail";
    all_ok = all_ok && cachedMinioOk;

    root["status"] = all_ok ? "healthy" : "degraded";
    return root;
}

}  // namespace

void SystemController::health(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback) {
    (void)req;
    Json::Value health = buildDeepHealth();
    auto resp = HttpResponse::newHttpJsonResponse(health);
    if (health["status"].asString() == "healthy") {
        resp->setStatusCode(k200OK);
    } else {
        resp->setStatusCode(k503ServiceUnavailable);
    }
    callback(resp);
}

void SystemController::stats(const HttpRequestPtr& req,
                             std::function<void(const HttpResponsePtr&)>&& callback) {
    (void)req;
    respond(handleStats(), std::move(callback));
}

void SystemController::metrics(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) {
        respond(auth, std::move(callback));
        return;
    }

    std::string result = MetricsCollector::instance().getMetricsJson();
    respond(HandlerResult::ok(result), std::move(callback));
}

void SystemController::cleanup(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) {
        respond(auth, std::move(callback));
        return;
    }

    int adminUserId = Config::instance().getInt("security.admin_user_id", 1);
    if (user_id != adminUserId) {
        respond(HandlerResult::error(errorResponse("Admin access required"), 403), std::move(callback));
        return;
    }

    cleanupOrphanChunks();

    Json::Value result;
    result["status"] = "success";
    result["message"] = "Orphan chunk cleanup triggered";
    auto resp = HttpResponse::newHttpJsonResponse(result);
    resp->setStatusCode(k200OK);
    callback(resp);
}

void SystemController::shutdown(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) {
        respond(auth, std::move(callback));
        return;
    }

    int adminUserId = Config::instance().getInt("security.admin_user_id", 1);
    if (user_id != adminUserId) {
        respond(HandlerResult::error(errorResponse("Admin access required"), 403), std::move(callback));
        return;
    }

    Json::Value result;
    result["status"] = "success";
    result["message"] = "Server shutting down";
    auto resp = HttpResponse::newHttpJsonResponse(result);
    resp->setStatusCode(k200OK);
    callback(resp);

    drogon::app().getLoop()->runAfter(0.5, []() {
        drogon::app().quit();
    });
}
