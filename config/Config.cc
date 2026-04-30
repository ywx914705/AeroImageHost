#include "Config.hpp"
#include <fstream>
#include <iostream>

Config& Config::instance() {
    static Config inst;
    return inst;
}

bool Config::load(const std::string& filename) {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        std::cerr << "Cannot open config file: " << filename << std::endl;
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    doc_.Parse(content.c_str());
    if (doc_.HasParseError()) {
        std::cerr << "JSON parse error" << std::endl;
        return false;
    }
    return true;
}

static const rapidjson::Value* getValue(const rapidjson::Value& root, const std::string& path) {
    const rapidjson::Value* cur = &root;
    size_t start = 0;
    while (true) {
        size_t dot = path.find('.', start);
        std::string key = path.substr(start, dot - start);
        if (!cur->IsObject() || !cur->HasMember(key.c_str())) return nullptr;
        cur = &(*cur)[key.c_str()];
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return cur;
}

int Config::getInt(const std::string& path, int defaultVal) const {
    const auto* val = getValue(doc_, path);
    if (val && val->IsInt()) return val->GetInt();
    return defaultVal;
}

std::string Config::getString(const std::string& path, const std::string& defaultVal) const {
    const auto* val = getValue(doc_, path);
    if (val && val->IsString()) return val->GetString();
    return defaultVal;
}

bool Config::getBool(const std::string& path, bool defaultVal) const {
    const auto* val = getValue(doc_, path);
    if (val && val->IsBool()) return val->GetBool();
    return defaultVal;
}