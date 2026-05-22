#include "FileAccessHandler.hpp"
#include "Config.hpp"
#include "Log.hpp"
#include "Auth.hpp"
#include "FileMeta.hpp"
#include "MinIOClient.hpp"
#include "ImageProcessor.hpp"
#include "WatermarkProcessor.hpp"
#include "Utils.hpp"
#include "MetricsCollector.hpp"
#include "AeroQueue.hpp"
#include "RedisClient.hpp"
#include <drogon/drogon.h>

using namespace rapidjson;

void fileAccessHandler(const drogon::HttpRequestPtr &req,
                       std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                       const std::string &file_id) {
    std::string auth = req->getHeader("Authorization");
    if (auth.empty()) auth = req->getHeader("authorization");
    auto user = Auth::verify(auth);
    int user_id = user ? user->user_id : 0;

    FileMeta meta = FileMetaDAO::instance().get(file_id);
    if (meta.file_id.empty()) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k404NotFound);
        resp->setBody(errorResponse("File not found"));
        callback(resp);
        return;
    }

    if (!meta.is_public && meta.user_id != user_id) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k403Forbidden);
        resp->setBody(errorResponse("Access denied"));
        callback(resp);
        return;
    }

    bool isImageFile = isImage(meta.mime_type);
    if (isImageFile) {
        std::string etag = "W/\"" + file_id + "\"";
        auto ifNoneMatch = req->getHeader("If-None-Match");
        if (!ifNoneMatch.empty() && ifNoneMatch == etag) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k304NotModified);
            resp->addHeader("Cache-Control", "public, max-age=3600");
            resp->addHeader("ETag", etag);
            callback(resp);
            return;
        }
    }

    RedisClient::instance().incr("file_views:" + file_id);
    AeroQueue::instance().post([file_id]() {
        RedisClient::instance().sadd("file_views_keys", "file_views:" + file_id);
    });

    std::string sizeParam = req->getParameter("size");
    bool isThumbRequest = false;
    if (!sizeParam.empty()) {
        int thumbSize = 0;
        try { thumbSize = std::stoi(sizeParam); } catch (...) {}
        if (thumbSize > 0 && thumbSize <= 2000) {
            isThumbRequest = true;
            std::string thumbKey = "thumbs/" + file_id + "_" + std::to_string(thumbSize);
            if (MinIOClient::instance().objectExists(thumbKey)) {
                std::string thumbUrl = MinIOClient::instance().presignGetUrl(thumbKey, 3600);
                if (!thumbUrl.empty()) {
                    auto resp = drogon::HttpResponse::newHttpResponse();
                    resp->setStatusCode(drogon::k302Found);
                    resp->addHeader("Location", thumbUrl);
                    resp->addHeader("Cache-Control", "public, max-age=86400");
                    callback(resp);
                    return;
                }
            }
            std::string lockKey = "thumbgen:" + file_id + ":" + std::to_string(thumbSize);
            constexpr int kThumbGenLockSec = 300;
            if (RedisClient::instance().setNxEx(lockKey, "1", kThumbGenLockSec)) {
                AeroQueue::instance().post([file_id, thumbSize, lockKey]() {
                    try {
                        std::vector<char> srcData;
                        if (!MinIOClient::instance().getObject(file_id, srcData) || srcData.empty()) {
                            RedisClient::instance().del(lockKey);
                            return;
                        }
                        std::vector<char> thumbData;
                        if (!ImageProcessor::generateThumbnail(srcData, thumbData, thumbSize, thumbSize)) {
                            RedisClient::instance().del(lockKey);
                            return;
                        }
                        std::vector<unsigned char> uploadData(thumbData.begin(), thumbData.end());
                        std::string genThumbKey = "thumbs/" + file_id + "_" + std::to_string(thumbSize);
                        if (!MinIOClient::instance().putObject(genThumbKey, uploadData, "image/jpeg")) {
                            RedisClient::instance().del(lockKey);
                        }
                    } catch (const std::exception& e) {
                        RedisClient::instance().del(lockKey);
                        AERO_LOG_ERROR("[Thumbnail] Generation failed for " + file_id + ": " + std::string(e.what()));
                    }
                });
            }
        }
    }

    std::string safeFilename = sanitizeFilename(meta.filename);
    std::string encodedFilename = urlEncode(safeFilename);
    std::string disposition;
    if (isAttachmentType(meta.mime_type) || !isImageFile) {
        disposition = "attachment; filename=\"" + safeFilename + "\"; filename*=UTF-8''" + encodedFilename;
    } else {
        disposition = "inline; filename=\"" + safeFilename + "\"; filename*=UTF-8''" + encodedFilename;
    }

    std::string targetFileId = file_id;
    if (isImageFile && !isThumbRequest) {
        std::string watermarkText, watermarkPosition;
        int watermarkOpacity;
        bool hasWatermark = FileMetaDAO::instance().getWatermark(file_id, watermarkText, watermarkPosition, watermarkOpacity);
        if (hasWatermark && !watermarkText.empty()) {
            std::string watermarkKey = file_id + "_watermark";
            bool exists = MinIOClient::instance().objectExists(watermarkKey);
            if (!exists) {
                std::vector<char> srcDataRaw;
                if (MinIOClient::instance().getObject(file_id, srcDataRaw) && !srcDataRaw.empty()) {
                    std::vector<unsigned char> srcData(srcDataRaw.begin(), srcDataRaw.end());
                    std::vector<unsigned char> dstData;
                    if (WatermarkProcessor::addTextWatermark(srcData, dstData, watermarkText, watermarkPosition, watermarkOpacity)) {
                        exists = MinIOClient::instance().putObject(watermarkKey, dstData, "image/jpeg");
                    }
                }
            }
            if (exists) {
                targetFileId = watermarkKey;
            }
        }
    }

    std::string presignUrl = MinIOClient::instance().presignGetUrl(targetFileId, 3600, disposition);
    if (presignUrl.empty()) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k500InternalServerError);
        resp->setBody(errorResponse("Failed to generate download URL"));
        callback(resp);
        return;
    }

    MetricsCollector::instance().recordBytes(false, static_cast<size_t>(meta.size));

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k302Found);
    resp->addHeader("Location", presignUrl);
    if (isImageFile && !isThumbRequest) {
        resp->addHeader("ETag", "W/\"" + file_id + "\"");
        resp->addHeader("Cache-Control", "public, max-age=3600");
    }
    callback(resp);
}
