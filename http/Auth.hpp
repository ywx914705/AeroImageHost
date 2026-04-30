#pragma once
#include <string>
#include <memory>
#include <cpprest/http_msg.h>

struct UserInfo {
    int user_id;
};

class Auth {
public:
    static std::shared_ptr<UserInfo> verify(const web::http::http_request& req);
    static std::string generateToken(int user_id, const std::string& username);
    static void revokeToken(const std::string& token);
};
