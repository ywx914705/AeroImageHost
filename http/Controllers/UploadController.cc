#include "UploadController.h"
#include "Handlers.hpp"
#include "ControllerHelpers.hpp"
#include <json/json.h>

void UploadController::upload(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    MultiPartParser parser;
    if (parser.parse(req) != 0 || parser.getFiles().empty()) {
        respond(HandlerResult::error(errorResponse("Missing file"), 400), std::move(callback));
        return;
    }

    auto& file = parser.getFiles()[0];
    std::string filename = file.getFileName();
    std::string contentType = req->getHeader("Content-Type");
    if (contentType.empty()) contentType = "application/octet-stream";

    size_t len = file.fileLength();
    const char* data = file.fileData();
    std::vector<unsigned char> fileData(data, data + len);

    respond(handleUpload(user_id, filename, fileData, contentType), std::move(callback));
}

void UploadController::presign(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    respond(handleRequestUploadUrl(user_id,
                                   json.get("filename", "").asString(),
                                   json.get("content_type", "").asString(),
                                   json.get("file_size", 0).asUInt64()),
            std::move(callback));
}

void UploadController::confirm(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    respond(handleConfirmUpload(user_id,
                                json.get("file_id", "").asString(),
                                json.get("filename", "").asString(),
                                json.get("content_type", "").asString(),
                                json.get("file_size", 0).asUInt64()),
            std::move(callback));
}

void UploadController::multipartInit(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    respond(handleMultipartInit(user_id,
                                json.get("filename", "").asString(),
                                json.get("content_type", "").asString(),
                                json.get("file_size", 0).asUInt64()),
            std::move(callback));
}

void UploadController::multipartChunk(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    std::string upload_id = json.get("upload_id", "").asString();
    int part_number = json.get("part_number", 0).asInt();

    std::string base64Data = json.get("data", "").asString();
    std::vector<unsigned char> data;
    if (!base64Data.empty()) {
        auto decoded = drogon::utils::base64DecodeToVector(base64Data);
        data.assign(decoded.begin(), decoded.end());
    }

    if (data.empty()) {
        MultiPartParser parser;
        if (parser.parse(req) == 0 && !parser.getFiles().empty()) {
            auto& file = parser.getFiles()[0];
            size_t len = file.fileLength();
            const char* fdata = file.fileData();
            data.assign(fdata, fdata + len);
        }
    }

    respond(handleMultipartUploadChunk(user_id, upload_id, part_number, data), std::move(callback));
}

void UploadController::multipartComplete(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    respond(handleMultipartComplete(user_id,
                                    json.get("upload_id", "").asString(),
                                    json.get("filename", "").asString(),
                                    json.get("content_type", "").asString(),
                                    json.get("file_size", 0).asUInt64(),
                                    json.get("total_chunks", 0).asInt()),
            std::move(callback));
}

void UploadController::multipartCleanup(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    int user_id;
    auto auth = requireAuth(req, user_id);
    if (auth.status_code != 200) { respond(auth, std::move(callback)); return; }

    Json::Value json;
    auto check = requireJson(req, json);
    if (check.status_code != 200) { respond(check, std::move(callback)); return; }

    respond(handleMultipartCleanup(user_id, json.get("upload_id", "").asString()), std::move(callback));
}
