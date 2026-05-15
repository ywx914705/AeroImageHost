#include "ClientIp.hpp"
#include "ForwardedForParse.hpp"

std::string client_ip_from_request(const drogon::HttpRequestPtr& req) {
    if (!req) {
        return "unknown";
    }
    std::string real = trim_http_ows(req->getHeader("X-Real-IP"));
    if (!real.empty() && real != "unknown") {
        return real;
    }
    std::string from_xff = first_ip_from_x_forwarded_for(req->getHeader("X-Forwarded-For"));
    if (!from_xff.empty()) {
        return from_xff;
    }
    return req->getPeerAddr().toIp();
}
