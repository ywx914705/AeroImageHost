#pragma once
#include <string>
#include <vector>
#include <ctime>
#include <rapidjson/document.h>

std::string generateUUID();
std::string getCurrentTime();
std::string getFileExtension(const std::string& filename);
bool isImage(const std::string& mime);
bool isAllowedExtension(const std::string& ext, const std::vector<std::string>& allowed);
std::string serializeJson(const rapidjson::Document& doc);

// URL 编码/解码（完整 RFC 3986 实现）
std::string urlEncode(const std::string& value);
std::string urlDecode(const std::string& value);

// 判断是否应该以附件形式下载
bool isAttachmentType(const std::string& mime_type);

// 根据文件扩展名获取正确的 MIME 类型
std::string getMimeTypeFromExtension(const std::string& filename);
bool isValidImageMimeType(const std::string& mime);

// Redis 键名生成
std::string getUserFilesKey(int userId, int page, int pageSize);
std::string getFileViewsKey(const std::string& fileId);