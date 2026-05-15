/*
 * MinIOClient 模块 - MinIO 对象存储客户端实现
 *
 * 职责：封装 MinIO S3 兼容 API，提供文件的增删改查和预签名 URL 生成。
 *
 * 核心功能：
 *   - putObject(): 上传文件到 MinIO（支持 char 和 unsigned char 两种重载）
 *   - getObject(): 下载文件到内存（用于缩略图生成）
 *   - deleteObject(): 删除文件
 *   - objectExists(): 检查文件是否存在
 *   - presignPutUrl(): 生成预签名上传 URL（客户端直传）
 *   - presignGetUrl(): 生成预签名下载 URL（支持 Content-Disposition）
 *   - composeObjects(): 合并多个分片为完整文件（分片上传的核心）
 *
 * 设计：单例模式，支持两个 MinIO 端点（实际端点 + 预签名端点）。
 *       使用 ZeroCopyStreambuf 避免上传时的内存拷贝。
 */
#include "Config.hpp"
#include "MinIOClient.hpp"
#include "Log.hpp"
#include <sstream>
#include <streambuf>
#include <iostream>
#include <cstring>
#include <istream>
#include <thread>

MinIOClient& MinIOClient::instance() {
    static MinIOClient inst;
    return inst;
}

bool MinIOClient::init(const std::string& endpoint, const std::string& accessKey,
                       const std::string& secretKey, const std::string& bucket,
                       const std::string& presign_endpoint) {
    endpoint_ = endpoint;
    presign_endpoint_ = presign_endpoint.empty() ? endpoint : presign_endpoint;
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

        // 如果指定了不同的 presign endpoint，创建用于预签名的独立客户端
        if (presign_endpoint_ != endpoint_) {
            std::string presign_host = presign_endpoint_;
            bool presign_https = true;
            if (presign_host.find("https://") == 0) {
                presign_https = true;
                presign_host = presign_host.substr(8);
            } else if (presign_host.find("http://") == 0) {
                presign_https = false;
                presign_host = presign_host.substr(7);
            }
            // 去除末尾斜杠
            if (!presign_host.empty() && presign_host.back() == '/') {
                presign_host.pop_back();
            }
            minio::s3::BaseUrl presignBaseUrl(presign_host, presign_https);
            presign_client_ = std::make_unique<minio::s3::Client>(presignBaseUrl, provider_.get());
        }

        LOG_INFO("MinIO client initialized: endpoint=" + endpoint_ +
                 " presign=" + presign_endpoint_ + " bucket=" + bucket_);

        // 启动时验证 bucket 是否存在，不存在则自动创建
        try {
            minio::s3::BucketExistsArgs beArgs;
            beArgs.bucket = bucket_;
            auto beResp = client_->BucketExists(beArgs);
            if (!beResp.exist) {
                minio::s3::MakeBucketArgs mbArgs;
                mbArgs.bucket = bucket_;
                auto mbResp = client_->MakeBucket(mbArgs);
                if (mbResp) {
                    LOG_INFO("MinIO bucket '" + bucket_ + "' 创建成功");
                } else {
                    LOG_WARN("MinIO bucket '" + bucket_ + "' 创建失败: " + mbResp.Error().String());
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("MinIO bucket 检查失败: " + std::string(e.what()));
        }

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("MinIO client init failed: " + std::string(e.what()));
        return false;
    }
}

// 零拷贝流缓冲区，直接包装内存数据为 istream，避免 vector 拷贝
struct ZeroCopyStreambuf : std::streambuf {
    explicit ZeroCopyStreambuf(const char* data, size_t len) {
        setg(const_cast<char*>(data), const_cast<char*>(data), const_cast<char*>(data + len));
    }
};

bool MinIOClient::retryOp(const std::function<bool()>& op, const std::string& name, int maxRetries) {
    for (int i = 0; i < maxRetries; ++i) {
        if (op()) return true;
        if (i < maxRetries - 1) {
            int delayMs = 200 * (1 << i);
            LOG_WARN("[MinIO] " + name + " failed, retry " +
                     std::to_string(i + 1) + "/" + std::to_string(maxRetries - 1) +
                     " after " + std::to_string(delayMs) + "ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
    }
    return false;
}

// 上传文件到 MinIO（接受 char 向量，用于分片上传等场景）
bool MinIOClient::putObject(const std::string& key, const std::vector<char>& data,
                            const std::string& contentType) {
    if (!client_) {
        LOG_ERROR("MinIO client 未初始化");
        return false;
    }

    return retryOp([&]() -> bool {
        try {
            ZeroCopyStreambuf buf(data.data(), data.size());
            std::istream iss(&buf);
            minio::s3::PutObjectArgs args(iss, static_cast<long>(data.size()), 10 * 1024 * 1024);
            args.bucket = bucket_;
            args.object = key;
            args.content_type = contentType.empty() ? "application/octet-stream" : contentType;
            minio::s3::PutObjectResponse resp = client_->PutObject(args);
            if (resp) {
                LOG_INFO("MinIO PutObject 成功: " + key + " (" + std::to_string(data.size()) + " bytes)");
                return true;
            } else {
                LOG_ERROR("MinIO PutObject 失败: " + resp.Error().String());
                return false;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("MinIO PutObject 异常: " + std::string(e.what()));
            return false;
        }
    }, "PutObject:" + key);
}

// 零拷贝上传：直接接受 HTTP 层的 unsigned char 向量，省掉一次内存拷贝
bool MinIOClient::putObject(const std::string& key, const std::vector<unsigned char>& data,
                            const std::string& contentType) {
    if (!client_) {
        LOG_ERROR("MinIO client 未初始化");
        return false;
    }

    return retryOp([&]() -> bool {
        try {
            ZeroCopyStreambuf buf(reinterpret_cast<const char*>(data.data()), data.size());
            std::istream iss(&buf);
            minio::s3::PutObjectArgs args(iss, static_cast<long>(data.size()), 10 * 1024 * 1024);
            args.bucket = bucket_;
            args.object = key;
            args.content_type = contentType.empty() ? "application/octet-stream" : contentType;
            minio::s3::PutObjectResponse resp = client_->PutObject(args);
            if (resp) {
                LOG_INFO("MinIO PutObject(uc) 成功: " + key + " (" + std::to_string(data.size()) + " bytes)");
                return true;
            } else {
                LOG_ERROR("MinIO PutObject(uc) 失败: " + resp.Error().String());
                return false;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("MinIO PutObject(uc) 异常: " + std::string(e.what()));
            return false;
        }
    }, "PutObject(uc):" + key);
}

std::string MinIOClient::presignPutUrl(const std::string& key, int expires) {
    auto* cli = presign_client_ ? presign_client_.get() : client_.get();
    if (!cli) return "";

    try {
        minio::s3::GetPresignedObjectUrlArgs args;
        args.method = minio::http::Method::kPut;
        args.bucket = bucket_;
        args.object = key;
        args.expiry_seconds = static_cast<unsigned int>(expires);

        minio::s3::GetPresignedObjectUrlResponse resp = cli->GetPresignedObjectUrl(args);

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
    auto* cli = presign_client_ ? presign_client_.get() : client_.get();
    if (!cli) return "";

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

        minio::s3::GetPresignedObjectUrlResponse resp = cli->GetPresignedObjectUrl(args);

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

    return retryOp([&]() -> bool {
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
    }, "deleteObject:" + key);
}

bool MinIOClient::objectExists(const std::string& key) {
    if (!client_) return false;

    return retryOp([&]() -> bool {
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
    }, "objectExists:" + key, 2);
}

bool MinIOClient::getObject(const std::string& key, std::vector<char>& data) {
    if (!client_) {
        LOG_ERROR("MinIO client not initialized");
        return false;
    }

    return retryOp([&]() -> bool {
        try {
            minio::s3::GetObjectArgs args;
            args.bucket = bucket_;
            args.object = key;
            data.clear();
            args.datafunc = [&data](minio::http::DataFunctionArgs chunk) -> bool {
                data.insert(data.end(), chunk.datachunk.data(),
                            chunk.datachunk.data() + chunk.datachunk.size());
                return true;
            };
            minio::s3::GetObjectResponse resp = client_->GetObject(args);
            if (resp) {
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
    }, "getObject:" + key);
}

bool MinIOClient::composeObjects(const std::string& destKey, const std::string& contentType,
                                 const std::vector<std::string>& sourceKeys) {
    if (!client_) {
        LOG_ERROR("MinIO client not initialized");
        return false;
    }

    return retryOp([&]() -> bool {
        try {
            minio::s3::ComposeObjectArgs args;
            args.bucket = bucket_;
            args.object = destKey;
            if (!contentType.empty()) {
                args.headers.Add("Content-Type", contentType);
            }
            for (const auto& key : sourceKeys) {
                minio::s3::ComposeSource source;
                source.bucket = bucket_;
                source.object = key;
                args.sources.push_back(source);
            }
            minio::s3::ComposeObjectResponse resp = client_->ComposeObject(args);
            if (resp) {
                LOG_INFO("MinIO ComposeObject success: " + destKey + " from " +
                         std::to_string(sourceKeys.size()) + " parts");
                return true;
            } else {
                LOG_ERROR("MinIO ComposeObject failed: " + resp.Error().String());
                return false;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("MinIO ComposeObject exception: " + std::string(e.what()));
            return false;
        }
    }, "composeObjects:" + destKey);
}

bool MinIOClient::isHealthy() {
    if (!client_ || bucket_.empty()) {
        return false;
    }
    return retryOp(
        [&]() -> bool {
            try {
                minio::s3::BucketExistsArgs beArgs;
                beArgs.bucket = bucket_;
                auto beResp = client_->BucketExists(beArgs);
                return static_cast<bool>(beResp) && beResp.exist;
            } catch (...) {
                return false;
            }
        },
        "isHealthy", 1);
}
