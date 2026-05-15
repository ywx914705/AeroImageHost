#pragma once

#include "RedisClient.hpp"

class RedisGuard {
public:
    RedisGuard() : ctx_(RedisClient::instance().getContext()) {}
    ~RedisGuard() {
        if (ctx_) {
            RedisClient::instance().releaseContext(ctx_);
        }
    }

    redisContext* get() { return ctx_; }
    explicit operator bool() const { return ctx_ != nullptr; }

    RedisGuard(const RedisGuard&) = delete;
    RedisGuard& operator=(const RedisGuard&) = delete;

private:
    redisContext* ctx_;
};
