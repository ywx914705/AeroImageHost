#include "AuthController.h"
#include "Handlers.hpp"
#include "ControllerHelpers.hpp"
#include "ClientIp.hpp"
#include <json/json.h>

void AuthController::registerUser(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    respond(handleRegister(json.get("account", "").asString(),
                           json.get("password", "").asString()),
            std::move(callback));
}

void AuthController::login(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    respond(handleLogin(json.get("account", "").asString(),
                        json.get("password", "").asString(),
                        client_ip_from_request(req)),
            std::move(callback));
}

void AuthController::sendCode(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    respond(handleSendVerificationCode(json.get("email", "").asString(),
                                       client_ip_from_request(req)),
            std::move(callback));
}

void AuthController::emailRegister(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    respond(handleEmailRegister(json.get("account", "").asString(),
                                json.get("password", "").asString(),
                                json.get("email", "").asString(),
                                json.get("code", "").asString()),
            std::move(callback));
}
