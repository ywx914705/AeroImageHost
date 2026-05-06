/*
 * Handlers.hpp - 业务逻辑处理函数头文件
 *
 * 职责：定义所有业务逻辑处理函数的声明。
 *       这些函数被 HttpServer 的各个 handler 方法调用。
 *
 * 在项目中的作用：
 *   - Handlers 是无状态的业务逻辑层
 *   - 接收已解析的请求参数，执行业务逻辑，返回 JSON 响应
 *   - 调用 FileMetaDAO（MySQL）、MinIOClient（MinIO）、RedisClient（Redis）等模块
 *
 * 设计：所有函数为全局函数（非类方法），返回 web::json::value。
 */
#ifndef HANDLERS_HPP
#define HANDLERS_HPP

#include <string>
#include <vector>
#include <utility>
#include <cpprest/json.h>

// ========== 用户认证 ==========
web::json::value handleRegister(const std::string& account, const std::string& password);
web::json::value handleLogin(const std::string& account, const std::string& password);

// ========== 文件上传 ==========
// 直接上传：服务端接收文件数据，代理上传到 MinIO
web::json::value handleUpload(int user_id, const std::string& filename, const std::vector<unsigned char>& file_data, const std::string& content_type);
// 请求预签名上传 URL：生成 MinIO PUT URL，客户端直传
web::json::value handleRequestUploadUrl(int user_id, const std::string& filename, const std::string& content_type, size_t file_size);
// 确认预签名上传：客户端上传完成后，保存元数据到 MySQL
web::json::value handleConfirmUpload(int user_id, const std::string& file_id, const std::string& filename, const std::string& content_type, size_t file_size);

// ========== 分片上传 ==========
// 初始化分片上传：生成 upload_id，计算分片信息
web::json::value handleMultipartInit(int user_id, const std::string& filename, const std::string& content_type, size_t file_size);
// 上传单个分片：通过服务端代理上传到 MinIO
web::json::value handleMultipartUploadChunk(const std::string& upload_id, int part_number, const std::vector<unsigned char>& data);
// 完成分片上传：验证所有分片 → ComposeObject 合并 → 保存元数据
web::json::value handleMultipartComplete(int user_id, const std::string& upload_id, const std::string& filename, const std::string& content_type, size_t file_size, int total_chunks);
// 清理分片：删除指定 upload_id 的所有分片
web::json::value handleMultipartCleanup(int user_id, const std::string& upload_id);

// ========== 文件管理 ==========
// 获取文件列表（支持分页和文件名搜索）
web::json::value handleListFiles(int user_id, int offset, int limit, const std::string& search_keyword = "");
// 删除单个文件
web::json::value handleDeleteFile(int user_id, const std::string& file_id);
// 批量删除文件
web::json::value handleBatchDeleteFiles(int user_id, const std::vector<std::string>& file_ids);
// 获取文件（返回预签名 URL，用于 302 重定向）
std::pair<std::vector<char>, std::string> handleGetFile(const std::string& file_id, bool check_auth, int user_id, const std::string& user_agent);
// 获取文件分享链接
web::json::value handleShare(const std::string& file_id);
// 切换文件公开/私有状态
web::json::value handleSetPublic(int user_id, const std::string& file_id);
// 按需获取预签名 URL（避免文件列表时批量生成）
web::json::value handleGetPresignUrl(int user_id, const std::string& file_id);

// ========== 系统管理 ==========
// 获取系统统计信息（用户数、文件数、总大小）
web::json::value handleStats();
// 清理 MinIO 中不存在但数据库中仍有记录的孤儿文件
void cleanupOrphanChunks();

// ========== 邮箱注册 ==========
// 发送验证码邮件（异步）
web::json::value handleSendVerificationCode(const std::string& email);
// 邮箱验证码注册
web::json::value handleEmailRegister(const std::string& account, const std::string& password, const std::string& email, const std::string& code);

#endif
