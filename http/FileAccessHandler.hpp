#pragma once
#include <drogon/drogon.h>
#include <string>

void fileAccessHandler(const drogon::HttpRequestPtr &req,
                       std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                       const std::string &file_id);
