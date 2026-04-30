#pragma once
#include <string>
#include <rapidjson/document.h>

class Config {
public:
    static Config& instance();
    bool load(const std::string& filename);
    int getInt(const std::string& path, int defaultVal = 0) const;
    std::string getString(const std::string& path, const std::string& defaultVal = "") const;
    bool getBool(const std::string& path, bool defaultVal = false) const;

private:
    rapidjson::Document doc_;
    Config() = default;
};