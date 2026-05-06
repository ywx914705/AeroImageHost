/*
 * EmailSender 模块 - 邮件发送实现
 *
 * 职责：通过 SMTP 协议发送邮件（支持 SSL/TLS）。
 *
 * 核心功能：
 *   - sendVerificationEmail(): 发送验证码邮件（从 config.json 读取 SMTP 配置）
 *   - generateVerificationCode(): 生成指定位数的随机数字验证码
 *
 * 内部实现：
 *   - SMTPClient 类：完整的 SMTP 客户端，支持隐式 SSL（465 端口）
 *   - 支持 AUTH LOGIN 认证方式（Base64 编码用户名/密码）
 *   - 支持多行 SMTP 响应解析
 *
 * 在项目中的作用：
 *   - 用户请求发送验证码 → AeroQueue 异步调用 sendVerificationEmail()
 *   - 避免 SMTP 连接阻塞 HTTP 请求（~2 秒延迟通过异步消除）
 *
 * 设计：单例模式，SMTP 连接参数从 config.json 的 smtp.* 节读取。
 */
#include "EmailSender.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include <random>
#include <sstream>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <cstdint>

static std::string getCurrentTimeString() {
    time_t rawtime;
    struct tm* timeinfo;
    char buffer[80];

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", timeinfo);
    return std::string(buffer);
}

static std::string base64_encode(const std::string& input) {
    static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        result.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (result.size() % 4) {
        result.push_back('=');
    }
    return result;
}

EmailSender& EmailSender::instance() {
    static EmailSender inst;
    return inst;
}

std::string EmailSender::generateVerificationCode(int length) {
    const std::string chars = "0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.size() - 1);

    std::string code;
    for (int i = 0; i < length; i++) {
        code += chars[dis(gen)];
    }
    return code;
}

class SMTPClient {
private:
    int sockfd;
    SSL* ssl;
    SSL_CTX* ctx;
    bool use_ssl;

    bool connect_to_server(const std::string& host, int port) {
        struct addrinfo hints, *res, *p;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) {
            return false;
        }

        sockfd = -1;
        for (p = res; p != nullptr; p = p->ai_next) {
            sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sockfd == -1) continue;

            if (::connect(sockfd, p->ai_addr, p->ai_addrlen) == 0) {
                break;
            }
            close(sockfd);
            sockfd = -1;
        }

        freeaddrinfo(res);

        if (sockfd == -1) {
            return false;
        }

        return true;
    }

    bool send_command(const std::string& cmd) {
        std::string full_cmd = cmd;
        if (full_cmd.back() != '\n') {
            full_cmd += "\r\n";
        }

        int bytes_sent = 0;
        const char* data = full_cmd.c_str();
        int total_len = full_cmd.length();

        while (bytes_sent < total_len) {
            int len;
            if (use_ssl && ssl) {
                len = SSL_write(ssl, data + bytes_sent, total_len - bytes_sent);
            } else {
                len = write(sockfd, data + bytes_sent, total_len - bytes_sent);
            }

            if (len <= 0) {
                return false;
            }
            bytes_sent += len;
        }

        return true;
    }

    std::string receive_response() {
        char buffer[1024];
        std::string response;
        int bytes_read;

        struct timeval tv;
        tv.tv_sec = 10;  // 10秒超时
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        while (true) {
            if (use_ssl && ssl) {
                bytes_read = SSL_read(ssl, buffer, sizeof(buffer) - 1);
            } else {
                bytes_read = read(sockfd, buffer, sizeof(buffer) - 1);
            }

            if (bytes_read <= 0) {
                break;
            }

            buffer[bytes_read] = '\0';
            response += buffer;

            // 检查是否是完整的响应：SMTP 多行响应以 "NNN-" 开头，最后一行以 "NNN " 结尾
            // 检查 response 末尾是否匹配 "NNN " 格式（三位数字+空格），表示响应结束
            if (response.size() >= 4) {
                size_t lastLine = response.rfind("\r\n");
                std::string lastPart = (lastLine != std::string::npos) ?
                    response.substr(lastLine + 2) : response;
                if (lastPart.size() >= 4 &&
                    isdigit(lastPart[0]) && isdigit(lastPart[1]) && isdigit(lastPart[2]) &&
                    lastPart[3] == ' ') {
                    break;
                }
            }
        }

        return response;
    }

    bool check_response(const std::string& response, const char* expected_prefix) {
        if (response.empty()) return false;
        return response.find(expected_prefix) != std::string::npos;
    }

public:
    SMTPClient(bool use_ssl = true) : sockfd(-1), ssl(nullptr), ctx(nullptr), use_ssl(use_ssl) {}

    ~SMTPClient() {
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        if (ctx) {
            SSL_CTX_free(ctx);
        }
        if (sockfd != -1) {
            close(sockfd);
        }
    }

    bool connect(const std::string& host, int port) {
        if (!connect_to_server(host, port)) {
            LOG_ERROR("Failed to connect to server: " + host + ":" + std::to_string(port));
            return false;
        }

        if (use_ssl) {
            // SSL 初始化只需在进程启动时调用一次，通过 static 标志保证
            static bool ssl_initialized = []() {
                SSL_library_init();
                SSL_load_error_strings();
                OpenSSL_add_all_algorithms();
                return true;
            }();

            ctx = SSL_CTX_new(SSLv23_client_method());
            if (!ctx) {
                LOG_ERROR("Failed to create SSL context");
                return false;
            }

            ssl = SSL_new(ctx);
            if (!ssl) {
                LOG_ERROR("Failed to create SSL object");
                return false;
            }

            SSL_set_fd(ssl, sockfd);
            if (SSL_connect(ssl) != 1) {
                LOG_ERROR("SSL connection failed: " + std::string(ERR_error_string(ERR_get_error(), nullptr)));
                return false;
            }
        }

        std::string response = receive_response();
        if (!check_response(response, "220")) {
            LOG_ERROR("Unexpected server response: " + response);
            return false;
        }

        return true;
    }

    bool login(const std::string& username, const std::string& password) {
        // EHLO
        if (!send_command("EHLO example.com")) {
            LOG_ERROR("Failed to send EHLO command");
            return false;
        }
        std::string response = receive_response();
        if (!check_response(response, "250")) {
            LOG_ERROR("EHLO failed: " + response);
            return false;
        }

        // AUTH LOGIN
        if (!send_command("AUTH LOGIN")) {
            LOG_ERROR("Failed to send AUTH LOGIN command");
            return false;
        }
        response = receive_response();
        if (!check_response(response, "334")) {
            LOG_ERROR("AUTH LOGIN failed: " + response);
            return false;
        }

        // Username (base64 encoded)
        std::string username_b64 = base64_encode(username);
        if (!send_command(username_b64)) {
            LOG_ERROR("Failed to send username");
            return false;
        }
        response = receive_response();
        if (!check_response(response, "334")) {
            LOG_ERROR("Username failed: " + response);
            return false;
        }

        // Password (base64 encoded)
        std::string password_b64 = base64_encode(password);
        if (!send_command(password_b64)) {
            LOG_ERROR("Failed to send password");
            return false;
        }
        response = receive_response();
        if (!check_response(response, "235")) {
            LOG_ERROR("Password failed: " + response);
            return false;
        }

        return true;
    }

    bool send_mail(const std::string& from, const std::string& to, const std::string& subject, const std::string& body) {
        // MAIL FROM
        std::string mail_from_cmd = "MAIL FROM:<" + from + ">";
        if (!send_command(mail_from_cmd)) {
            LOG_ERROR("Failed to send MAIL FROM command");
            return false;
        }
        std::string response = receive_response();
        if (!check_response(response, "250")) {
            LOG_ERROR("MAIL FROM failed: " + response);
            return false;
        }

        // RCPT TO
        std::string rcpt_to_cmd = "RCPT TO:<" + to + ">";
        if (!send_command(rcpt_to_cmd)) {
            LOG_ERROR("Failed to send RCPT TO command");
            return false;
        }
        response = receive_response();
        if (!check_response(response, "250")) {
            LOG_ERROR("RCPT TO failed: " + response);
            return false;
        }

        // DATA
        if (!send_command("DATA")) {
            LOG_ERROR("Failed to send DATA command");
            return false;
        }
        response = receive_response();
        if (!check_response(response, "354")) {
            LOG_ERROR("DATA failed: " + response);
            return false;
        }

        // Email content
        std::string mail_content =
            "Date: " + getCurrentTimeString() + "\r\n"
            "To: <" + to + ">\r\n"
            "From: AeroImageHost <" + from + ">\r\n"
            "Subject: " + subject + "\r\n"
            "MIME-Version: 1.0\r\n"
            "Content-Type: text/plain; charset=UTF-8\r\n"
            "\r\n"
            + body + "\r\n"
            ".\r\n";

        if (!send_command(mail_content)) {
            LOG_ERROR("Failed to send email content");
            return false;
        }

        response = receive_response();
        if (!check_response(response, "250")) {
            LOG_ERROR("Email send failed: " + response);
            return false;
        }

        return true;
    }
};

bool EmailSender::sendVerificationEmail(const std::string& to_email, const std::string& code) {
    // 从配置中获取SMTP设置
    std::string smtp_server = Config::instance().getString("smtp.server", "smtp.qq.com");
    int smtp_port = Config::instance().getInt("smtp.port", 465);
    std::string smtp_username = Config::instance().getString("smtp.username", "");
    std::string smtp_password = Config::instance().getString("smtp.password", "");
    std::string from_email = Config::instance().getString("smtp.from", smtp_username);

    if (smtp_username.empty() || smtp_password.empty()) {
        LOG_WARN("SMTP credentials not configured");
        return false;
    }

    // 创建SMTP客户端
    SMTPClient client(smtp_port == 465); // 465端口使用隐式SSL

    try {
        // 连接到服务器
        if (!client.connect(smtp_server, smtp_port)) {
            LOG_ERROR("Failed to connect to SMTP server: " + smtp_server + ":" + std::to_string(smtp_port));
            return false;
        }

        // 登录
        if (!client.login(smtp_username, smtp_password)) {
            LOG_ERROR("SMTP login failed for user: " + smtp_username);
            return false;
        }

        // 发送邮件
        std::string subject = "AeroImageHost 验证码";
        std::string body =
            "您好！\r\n\r\n"
            "感谢您注册 AeroImageHost。\r\n"
            "您的验证码是：" + code + "\r\n"
            "验证码有效期为10分钟，请尽快使用。\r\n\r\n"
            "如果您没有进行此操作，请忽略此邮件。\r\n\r\n"
            "AeroImageHost 团队";

        if (!client.send_mail(from_email, to_email, subject, body)) {
            LOG_ERROR("Failed to send email to: " + to_email);
            return false;
        }

        LOG_INFO("Verification email sent successfully to: " + to_email);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("SMTP exception: " + std::string(e.what()));
        return false;
    } catch (...) {
        LOG_ERROR("Unknown SMTP exception");
        return false;
    }
}