#pragma once

#include <string>
#include <functional>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>
#include "Auth.hpp"
#include "HandlerResult.hpp"
#include "Utils.hpp"

inline int getUserIdFromToken(const drogon::HttpRequestPtr& req) {
    std::string auth = req->getHeader("Authorization");
    if (auth.empty()) auth = req->getHeader("authorization");
    auto user = Auth::verify(auth);
    return user ? user->user_id : 0;
}

inline drogon::HttpResponsePtr makeResponse(const HandlerResult& r) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(r.status_code));
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(r.body);
    return resp;
}

inline void respond(const HandlerResult& result,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    callback(makeResponse(result));
}

inline HandlerResult requireAuth(const drogon::HttpRequestPtr& req, int& user_id) {
    user_id = getUserIdFromToken(req);
    if (user_id == 0) {
        return HandlerResult::error(errorResponse("Unauthorized"), 401);
    }
    return HandlerResult::ok("");
}

inline HandlerResult requireJson(const drogon::HttpRequestPtr& req, Json::Value& json) {
    auto j = req->getJsonObject();
    if (!j) {
        return HandlerResult::error(errorResponse("Missing JSON body"), 400);
    }
    json = *j;
    return HandlerResult::ok("");
}
