/*
 * Config 模块 - JSON 配置文件加载器
 *
 * 职责：从 config.json 加载配置，支持点号分隔的路径访问（如 "mysql.host"）。
 * 设计：单例模式，全局唯一实例，所有模块通过 Config::instance() 获取配置。
 *
 * 在项目中的作用：所有外部服务的连接参数（MySQL、Redis、MinIO、SMTP）
 * 都从 config.json 读取，避免硬编码。
 */
#include "Config.hpp"
#include <fstream>
#include <iostream>

// 获取全局唯一的 Config 单例实例
Config& Config::instance() {
    static Config inst;
    return inst;
}

// 加载 JSON 配置文件，返回是否成功
bool Config::load(const std::string& filename) {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        std::cerr << "Cannot open config file: " << filename << std::endl;
        return false;
    }
    // 一次性读取整个文件内容
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    // 使用 RapidJSON 解析 JSON 内容
    doc_.Parse(content.c_str());
    if (doc_.HasParseError()) {
        std::cerr << "JSON parse error" << std::endl;
        return false;
    }
    return true;
}

// 内部辅助函数：按点号分隔路径逐层查找 JSON 值
// 例如路径 "mysql.host" 会先找 doc_["mysql"]，再找 ["host"]
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

// 获取整数配置值，路径不存在或类型不匹配时返回默认值
int Config::getInt(const std::string& path, int defaultVal) const {
    const auto* val = getValue(doc_, path);
    if (val && val->IsInt()) return val->GetInt();
    return defaultVal;
}

// 获取字符串配置值，路径不存在或类型不匹配时返回默认值
std::string Config::getString(const std::string& path, const std::string& defaultVal) const {
    const auto* val = getValue(doc_, path);
    if (val && val->IsString()) return val->GetString();
    return defaultVal;
}

// 获取布尔配置值，路径不存在或类型不匹配时返回默认值
bool Config::getBool(const std::string& path, bool defaultVal) const {
    const auto* val = getValue(doc_, path);
    if (val && val->IsBool()) return val->GetBool();
    return defaultVal;
}