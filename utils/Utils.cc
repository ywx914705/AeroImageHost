#include "Utils.hpp"
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_int_distribution<> dis(0, 15);
static std::uniform_int_distribution<> dis2(8, 11);

std::string generateUUID() {
    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; ++i) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; ++i) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; ++i) ss << dis(gen);
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; ++i) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; ++i) ss << dis(gen);
    return ss.str();
}

std::string getCurrentTime() {
    time_t now = time(nullptr);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return buf;
}

std::string getFileExtension(const std::string& filename) {
    size_t dot = filename.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

bool isImage(const std::string& mime) {
    return mime.find("image/") == 0;
}

bool isAllowedExtension(const std::string& ext, const std::vector<std::string>& allowed) {
    return std::find(allowed.begin(), allowed.end(), ext) != allowed.end();
}

std::string serializeJson(const rapidjson::Document& doc) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}

std::string getUserFilesKey(int userId, int page, int pageSize) {
    return "user_files:" + std::to_string(userId) + ":page:" + std::to_string(page) + ":size:" + std::to_string(pageSize);
}

std::string getFileViewsKey(const std::string& fileId) {
    return "file_views:" + fileId;
}

// RFC 3986 URL 编码（保留字符不被编码）
std::string urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        // 保留字符: A-Z a-z 0-9 - _ . ~
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            // 其他字符编码为 %XX
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    return escaped.str();
}

// URL 解码
std::string urlDecode(const std::string& value) {
    std::string result;
    result.reserve(value.length());

    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '%' && i + 2 < value.length()) {
            std::string hex = value.substr(i + 1, 2);
            char decoded = static_cast<char>(std::stoi(hex, nullptr, 16));
            result += decoded;
            i += 2;
        } else if (value[i] == '+') {
            result += ' ';
        } else {
            result += value[i];
        }
    }
    return result;
}

// 根据文件扩展名获取正确的 MIME 类型
bool isAttachmentType(const std::string& mime_type) {
    static const std::unordered_set<std::string> attachmentMimes = {
        "application/pdf",
        "application/msword",
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        "application/vnd.ms-excel",
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        "application/vnd.ms-powerpoint",
        "application/vnd.openxmlformats-officedocument.presentationml.presentation",
        "application/zip",
        "application/x-rar-compressed",
        "application/x-7z-compressed",
        "application/octet-stream"
    };
    return attachmentMimes.find(mime_type) != attachmentMimes.end();
}

std::string getMimeTypeFromExtension(const std::string& filename) {
    std::string ext = getFileExtension(filename);
    static const std::unordered_map<std::string, std::string> mimeMap = {
        // 图片
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"png", "image/png"},
        {"gif", "image/gif"},
        {"webp", "image/webp"},
        {"svg", "image/svg+xml"},
        {"bmp", "image/bmp"},
        {"ico", "image/x-icon"},
        {"tiff", "image/tiff"},
        {"tif", "image/tiff"},
        // 文档
        {"pdf", "application/pdf"},
        {"doc", "application/msword"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xls", "application/vnd.ms-excel"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"ppt", "application/vnd.ms-powerpoint"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {"txt", "text/plain"},
        {"csv", "text/csv"},
        {"json", "application/json"},
        {"xml", "application/xml"},
        // 压缩包
        {"zip", "application/zip"},
        {"rar", "application/x-rar-compressed"},
        {"7z", "application/x-7z-compressed"},
        {"gz", "application/gzip"},
        {"tar", "application/x-tar"},
        // 视频
        {"mp4", "video/mp4"},
        {"webm", "video/webm"},
        {"avi", "video/x-msvideo"},
        {"mov", "video/quicktime"},
        {"wmv", "video/x-ms-wmv"},
        // 音频
        {"mp3", "audio/mpeg"},
        {"wav", "audio/wav"},
        {"ogg", "audio/ogg"},
        {"flac", "audio/flac"},
    };

    auto it = mimeMap.find(ext);
    if (it != mimeMap.end()) {
        return it->second;
    }
    return "application/octet-stream";
}

bool isValidImageMimeType(const std::string& mime) {
    return mime == "image/jpeg" || mime == "image/png" ||
           mime == "image/gif" || mime == "image/webp" ||
           mime == "image/bmp" || mime == "image/svg+xml" ||
           mime == "image/tiff";
}