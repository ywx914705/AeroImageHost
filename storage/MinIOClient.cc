#include "Config.hpp"
#include "MinIOClient.hpp"
#include "Log.hpp"
#include <sstream>
#include <streambuf>
#include <iostream>
#include <cstring>
#include <istream>

MinIOClient& MinIOClient::instance() {
    static MinIOClient inst;
    return inst;
}

bool MinIOClient::init(const std::string& endpoint, const std::string& accessKey,
                       const std::string& secretKey, const std::string& bucket) {
    endpoint_ = endpoint;
    accessKey_ = accessKey;
    secretKey_ = secretKey;
    bucket_ = bucket;

    try {
        // 解析 endpoint (如 "http://127.0.0.1:9000")
        std::string host = endpoint_;
        bool https = true;
        if (host.find("https://") == 0) {
            https = true;
            host = host.substr(8);
        } else if (host.find("http://") == 0) {
            https = false;
            host = host.substr(7);
        }

        minio::s3::BaseUrl baseUrl(host, https);

        // 创建凭证提供者并保存为成员变量
        provider_ = std::make_unique<minio::creds::StaticProvider>(accessKey_, secretKey_, std::string());

        // 创建客户端
        client_ = std::make_unique<minio::s3::Client>(baseUrl, provider_.get());

        LOG_INFO("MinIO client initialized: " + endpoint_ + "/" + bucket_);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("MinIO client init failed: " + std::string(e.what()));
        return false;
    }
}

bool MinIOClient::putObject(const std::string& key, const std::vector<char>& data,
                            const std::string& contentType) {
    if (!client_) {
        LOG_ERROR("MinIO client not initialized");
        return false;
    }

    try {
        std::string upload_data(data.data(), data.size());

        // 使用 istringstream 而非 stringstream + write()（write后get position在末尾，SDK读不到数据）
        std::istringstream iss(upload_data);

        // 创建 PutObjectArgs 对象（part size 10MB，大于常见 PDF 避免触发 multipart）
        minio::s3::PutObjectArgs args(iss, static_cast<long>(upload_data.size()), 10 * 1024 * 1024);
        args.bucket = bucket_;
        args.object = key;
        args.content_type = contentType.empty() ? "application/octet-stream" : contentType;

        // 执行上传
        minio::s3::PutObjectResponse resp = client_->PutObject(args);

        if (resp) {
            LOG_INFO("MinIO PutObject success: " + key + " (" + std::to_string(data.size()) + " bytes)");
            return true;
        } else {
            LOG_ERROR("MinIO PutObject failed: " + resp.Error().String());
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MinIO PutObject exception: " + std::string(e.what()));
        return false;
    }
}

std::string MinIOClient::presignPutUrl(const std::string& key, int expires) {
    if (!client_) return "";

    try {
        minio::s3::GetPresignedObjectUrlArgs args;
        args.method = minio::http::Method::kPut;
        args.bucket = bucket_;
        args.object = key;
        args.expiry_seconds = static_cast<unsigned int>(expires);

        minio::s3::GetPresignedObjectUrlResponse resp = client_->GetPresignedObjectUrl(args);

        if (resp) {
            return resp.url;
        } else {
            LOG_ERROR("MinIO presign PUT failed: " + resp.Error().String());
            return "";
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MinIO presign PUT exception: " + std::string(e.what()));
        return "";
    }
}

std::string MinIOClient::presignGetUrl(const std::string& key, int expires,
                                       const std::string& content_disposition) {
    if (!client_) return "";

    try {
        minio::s3::GetPresignedObjectUrlArgs args;
        args.method = minio::http::Method::kGet;
        args.bucket = bucket_;
        args.object = key;
        args.expiry_seconds = static_cast<unsigned int>(expires);

        // 通过查询参数传递 Content-Disposition
        if (!content_disposition.empty()) {
            args.extra_query_params.Add("response-content-disposition", content_disposition);
        }

        minio::s3::GetPresignedObjectUrlResponse resp = client_->GetPresignedObjectUrl(args);

        if (resp) {
            return resp.url;
        } else {
            LOG_ERROR("MinIO presign GET failed: " + resp.Error().String());
            return "";
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MinIO presign GET exception: " + std::string(e.what()));
        return "";
    }
}

bool MinIOClient::deleteObject(const std::string& key) {
    if (!client_) return false;

    try {
        minio::s3::RemoveObjectArgs args;
        args.bucket = bucket_;
        args.object = key;

        minio::s3::RemoveObjectResponse resp = client_->RemoveObject(args);

        if (resp) {
            LOG_INFO("MinIO deleteObject success: " + key);
            return true;
        } else {
            LOG_ERROR("MinIO deleteObject failed: " + resp.Error().String());
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MinIO deleteObject exception: " + std::string(e.what()));
        return false;
    }
}

bool MinIOClient::objectExists(const std::string& key) {
    if (!client_) return false;

    try {
        minio::s3::StatObjectArgs args;
        args.bucket = bucket_;
        args.object = key;

        minio::s3::StatObjectResponse resp = client_->StatObject(args);
        return static_cast<bool>(resp);
    } catch (const std::exception& e) {
        LOG_ERROR("MinIO statObject exception: " + std::string(e.what()));
        return false;
    }
}

bool MinIOClient::getObject(const std::string& key, std::vector<char>& data) {
    if (!client_) {
        LOG_ERROR("MinIO client not initialized");
        return false;
    }

    try {
        minio::s3::GetObjectArgs args;
        args.bucket = bucket_;
        args.object = key;

        // 使用 stringstream 接收数据
        std::ostringstream oss;
        args.datafunc = [&oss](minio::http::DataFunctionArgs args) -> bool {
            oss.write(args.datachunk.data(), args.datachunk.size());
            return true;
        };

        minio::s3::GetObjectResponse resp = client_->GetObject(args);

        if (resp) {
            std::string result = oss.str();
            data.assign(result.begin(), result.end());
            LOG_INFO("MinIO GetObject success: " + key + " (" + std::to_string(data.size()) + " bytes)");
            return true;
        } else {
            LOG_ERROR("MinIO GetObject failed: " + resp.Error().String());
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MinIO GetObject exception: " + std::string(e.what()));
        return false;
    }
}
