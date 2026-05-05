#pragma once
#include <string>
#include <vector>
#include <memory>
#include <miniocpp/client.h>

class MinIOClient {
public:
    static MinIOClient& instance();
    bool init(const std::string& endpoint, const std::string& accessKey,
              const std::string& secretKey, const std::string& bucket,
              const std::string& presign_endpoint = "");

    // 上传文件到 MinIO
    bool putObject(const std::string& key, const std::vector<char>& data,
                   const std::string& contentType);
    // 零拷贝上传：直接接受 HTTP 层的 unsigned char，省掉一次内存拷贝
    bool putObject(const std::string& key, const std::vector<unsigned char>& data,
                   const std::string& contentType);

    // 获取预签名 PUT 和 GET URL
    std::string presignPutUrl(const std::string& key, int expires = 3600);
    std::string presignGetUrl(const std::string& key, int expires = 3600,
                              const std::string& content_disposition = "");

    // 删除和检查对象
    bool deleteObject(const std::string& key);
    bool objectExists(const std::string& key);

    // 下载对象到内存（用于缩略图生成）
    bool getObject(const std::string& key, std::vector<char>& data);

    // 拼接多个对象为一个（用于分片上传合并）
    bool composeObjects(const std::string& destKey, const std::string& contentType,
                        const std::vector<std::string>& sourceKeys);

private:
    std::string endpoint_;
    std::string presign_endpoint_;
    std::string accessKey_;
    std::string secretKey_;
    std::string bucket_;

    // MinIO SDK 客户端和凭证提供者
    std::unique_ptr<minio::s3::Client> client_;
    std::unique_ptr<minio::s3::Client> presign_client_;  // 用于生成预签名URL的客户端（指向外部域名）
    std::unique_ptr<minio::creds::Provider> provider_;
};
