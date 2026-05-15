#include "AuthFilter.h"
#include "Auth.hpp"
#include "ControllerHelpers.hpp"
#include "Log.hpp"

void AuthFilter::doFilter(const HttpRequestPtr &req,
                          FilterCallback &&fcb,
                          FilterChainCallback &&fccb) {
    std::string auth = req->getHeader("Authorization");
    if (auth.empty()) {
        auth = req->getHeader("authorization");
    }

    auto user = Auth::verify(auth);
    if (!user) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k401Unauthorized);
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        resp->setBody(errorResponse("Unauthorized"));
        fcb(resp);
        return;
    }

    req->addHeader("X-User-Id", std::to_string(user->user_id));
    fccb();
}
