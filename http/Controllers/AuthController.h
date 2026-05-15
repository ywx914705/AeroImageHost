/*
 * AuthController.h - 认证路由 Controller
 *
 * 路由：
 *   POST /api/auth/register       - 用户注册
 *   POST /api/auth/login          - 用户登录
 *   POST /api/auth/send-code      - 发送邮箱验证码
 *   POST /api/auth/email-register - 邮箱验证码注册
 */
#ifndef AUTH_CONTROLLER_H
#define AUTH_CONTROLLER_H

#include <drogon/HttpController.h>

using namespace drogon;

class AuthController : public drogon::HttpController<AuthController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::registerUser, "/api/auth/register", Post);
    ADD_METHOD_TO(AuthController::login, "/api/auth/login", Post);
    ADD_METHOD_TO(AuthController::sendCode, "/api/auth/send-code", Post);
    ADD_METHOD_TO(AuthController::emailRegister, "/api/auth/email-register", Post);
    METHOD_LIST_END

    void registerUser(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void login(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void sendCode(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void emailRegister(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
};

#endif // AUTH_CONTROLLER_H
