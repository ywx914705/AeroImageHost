#pragma once

#include <drogon/HttpRequest.h>
#include <string>

// Client IP for rate limiting / auditing: X-Real-IP, then first X-Forwarded-For hop, then TCP peer.
std::string client_ip_from_request(const drogon::HttpRequestPtr& req);
