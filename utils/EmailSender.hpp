#pragma once

#include <string>

class EmailSender {
public:
    static EmailSender& instance();

    bool sendVerificationEmail(const std::string& to_email, const std::string& code);
    static std::string generateVerificationCode(int length = 6);

private:
    EmailSender() = default;
    ~EmailSender() = default;
    EmailSender(const EmailSender&) = delete;
    EmailSender& operator=(const EmailSender&) = delete;
};