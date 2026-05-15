#pragma once
#include <string>
#include <utility>

struct HandlerResult {
    std::string body;
    int status_code = 200;

    static HandlerResult ok(std::string json) {
        return {std::move(json), 200};
    }
    static HandlerResult created(std::string json) {
        return {std::move(json), 201};
    }
    static HandlerResult error(std::string json, int code = 400) {
        return {std::move(json), code};
    }
};
