#pragma once

#include <string>
#include <string_view>

// Trim OWS (HTTP) from both ends; empty if only whitespace.
std::string trim_http_ows(std::string_view s);

// First hop in X-Forwarded-For (client when behind a trusted reverse proxy).
std::string first_ip_from_x_forwarded_for(std::string_view xff);
