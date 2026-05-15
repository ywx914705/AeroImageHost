/*
 * AuthFilter.h - JWT/Token 认证过滤器
 *
 * 在项目中的作用：
 *   作为 Drogon HttpFilter，拦截需要认证的 API 请求，
 *   验证 Authorization 头中的 Bearer Token 是否有效。
 *   验证通过后将 user_id 存入请求属性，供 Controller 使用。
 */
#pragma once

#include <drogon/HttpFilter.h>

using namespace drogon;

class AuthFilter : public HttpFilter<AuthFilter> {
public:
    AuthFilter() = default;
    virtual void doFilter(const HttpRequestPtr &req,
                          FilterCallback &&fcb,
                          FilterChainCallback &&fccb) override;
};
