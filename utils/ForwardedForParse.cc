#include "ForwardedForParse.hpp"

#include <cctype>

std::string trim_http_ows(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

std::string first_ip_from_x_forwarded_for(std::string_view xff) {
    if (xff.empty()) {
        return {};
    }
    auto comma = xff.find(',');
    std::string_view first = (comma == std::string_view::npos) ? xff : xff.substr(0, comma);
    return trim_http_ows(first);
}
