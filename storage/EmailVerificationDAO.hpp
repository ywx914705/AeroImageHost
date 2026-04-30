#pragma once

#include <string>
#include <cpprest/json.h>

class EmailVerificationDAO {
public:
    static EmailVerificationDAO& instance();

    bool save(const std::string& email, const std::string& code);
    bool verifyCode(const std::string& email, const std::string& code);
    bool isCodeUsed(const std::string& email, const std::string& code);
    void markCodeAsUsed(const std::string& email, const std::string& code);
    void cleanupExpired();

private:
    EmailVerificationDAO() = default;
    ~EmailVerificationDAO() = default;
    EmailVerificationDAO(const EmailVerificationDAO&) = delete;
    EmailVerificationDAO& operator=(const EmailVerificationDAO&) = delete;
};