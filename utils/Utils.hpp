/*
 * Utils.hpp - 通用工具函数头文件
 *
 * 职责：提供项目中广泛使用的工具函数，包括：
 *   - UUID 生成（文件唯一标识）
 *   - URL 编码/解码（RFC 3986 标准）
 *   - MIME 类型检测（根据文件扩展名推断）
 *   - 文件类型判断（图片、附件等）
 *   - JSON 序列化（RapidJSON → 字符串）
 *
 * 在项目中的作用：被 Handlers、HttpServer、FileMetaDAO 等多个模块调用。
 */
#pragma once
#include <string>
#include <vector>
#include <ctime>
#include <rapidjson/document.h>

// 生成 UUID v4 格式的随机字符串（如 "550e8400-e29b-41d4-a716-446655440000"）
std::string generateUUID();
// 获取当前时间的格式化字符串（"YYYY-MM-DD HH:MM:SS"）
std::string getCurrentTime();
// 获取文件扩展名（不含点号，小写，如 "jpg"、"png"）
std::string getFileExtension(const std::string& filename);
// 判断 MIME 类型是否为图片类型（以 "image/" 开头）
bool isImage(const std::string& mime);
// 检查文件扩展名是否在允许列表中
bool isAllowedExtension(const std::string& ext, const std::vector<std::string>& allowed);
// 将 RapidJSON Document 序列化为 JSON 字符串
std::string serializeJson(const rapidjson::Document& doc);

// URL 编码（RFC 3986 标准，保留 A-Z a-z 0-9 - _ . ~）
std::string urlEncode(const std::string& value);
// URL 解码（还原 %XX 编码和 + 号为空格）
std::string urlDecode(const std::string& value);

// 判断 MIME 类型是否应该以附件形式下载（Word、Excel、压缩包等）
bool isAttachmentType(const std::string& mime_type);

// 根据文件扩展名获取对应的 MIME 类型（如 "jpg" → "image/jpeg"）
std::string getMimeTypeFromExtension(const std::string& filename);
// 判断 MIME 类型是否为有效的图片类型（用于上传时验证客户端提供的 Content-Type）
bool isValidImageMimeType(const std::string& mime);

// Redis 键名生成：用户文件列表缓存 key
std::string getUserFilesKey(int userId, int page, int pageSize);
// Redis 键名生成：文件浏览次数 key
std::string getFileViewsKey(const std::string& fileId);

// 将 RapidJSON Document 序列化为 JSON 字符串（别名）
std::string docToString(const rapidjson::Document& doc);
// 创建错误响应 JSON 字符串
std::string errorResponse(const std::string& msg);
// 根据错误消息推断合适的 HTTP 状态码
int errorToHttpStatus(const std::string& errorMsg);
// 过滤文件名中的危险字符（控制字符、双引号、反斜杠），防止 HTTP 头注入
std::string sanitizeFilename(const std::string& filename);