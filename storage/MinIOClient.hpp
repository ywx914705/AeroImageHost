/*
 * MinIOClient.hpp - MinIO 对象存储客户端头文件
 *
 * 职责：封装 MinIO S3 兼容 API，提供文件的上传、下载、删除、预签名 URL 生成等功能。
 *
 * 在项目中的作用：
 *   - 文件上传：putObject() 将文件存储到 MinIO
 *   - 文件访问：presignGetUrl() 生成预签名 URL，让客户端直接从 MinIO 下载
 *   - 分片上传：composeObjects() 将多个分片合并为完整文件
 *   - 缩略图缓存：getObject() / putObject() 读写 thumbs/ 前缀的缓存
 *   - 文件删除：deleteObject() 从 MinIO 删除文件
 *
 * 设计：单例模式，支持两个 MinIO 端点：
 *   - endpoint_：实际的 MinIO 服务地址（用于上传/下载/删除）
 *   - presign_client_：预签名 URL 使用的地址（可指向 CDN 或反向代理）
 */
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <miniocpp/client.h>

class MinIOClient {
public:
    static MinIOClient& instance(); // 获取单例实例

    // 初始化客户端：传入 MinIO 地址、凭证、桶名，可选外部预签名端点
    bool init(const std::string& endpoint, const std::string& accessKey,
              const std::string& secretKey, const std::string& bucket,
              const std::string& presign_endpoint = "");

    // 上传文件到 MinIO（接受 char 向量，用于缩略图缓存等场景）
    bool putObject(const std::string& key, const std::vector<char>& data,
                   const std::string& contentType);
    // 零拷贝上传：直接接受 HTTP 层的 unsigned char，省掉一次内存拷贝
    bool putObject(const std::string& key, const std::vector<unsigned char>& data,
                   const std::string& contentType);

    // 生成预签名 PUT URL（用于客户端直传 MinIO）
    std::string presignPutUrl(const std::string& key, int expires = 3600);
    // 生成预签名 GET URL（用于文件下载/预览），可附加 Content-Disposition
    std::string presignGetUrl(const std::string& key, int expires = 3600,
                              const std::string& content_disposition = "");

    // 删除 MinIO 中的对象
    bool deleteObject(const std::string& key);
    // 检查对象是否存在
    bool objectExists(const std::string& key);

    // 存活探测：客户端已 init 且桶可访问（供 /api/health）
    bool isHealthy();

    // 下载对象到内存（用于缩略图生成等场景）
    bool getObject(const std::string& key, std::vector<char>& data);

    // 拼接多个对象为一个（用于分片上传合并，零下载合并）
    bool composeObjects(const std::string& destKey, const std::string& contentType,
                        const std::vector<std::string>& sourceKeys);

private:
    // 带指数退避的重试辅助方法（maxRetries 次重试，200/400ms 退避）
    bool retryOp(const std::function<bool()>& op, const std::string& name, int maxRetries = 3);

    std::string endpoint_;         // MinIO 服务实际地址
    std::string presign_endpoint_; // 预签名 URL 使用的外部地址（可选 CDN）
    std::string accessKey_;        // MinIO 访问密钥
    std::string secretKey_;        // MinIO 秘密密钥
    std::string bucket_;           // MinIO 存储桶名称

    // MinIO SDK 客户端和凭证提供者
    std::unique_ptr<minio::s3::Client> client_;          // 主客户端（上传/下载/删除）
    std::unique_ptr<minio::s3::Client> presign_client_;  // 预签名专用客户端（可指向外部域名）
    std::unique_ptr<minio::creds::Provider> provider_;   // 凭证提供者
};
