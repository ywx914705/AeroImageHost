/*
 * Config.hpp - 配置模块头文件
 *
 * 提供 JSON 配置文件的加载和访问接口。
 * 使用单例模式，支持点号分隔的路径（如 "redis.port"）访问嵌套 JSON 值。
 */
#pragma once
#include <string>
#include <rapidjson/document.h>

class Config {
public:
    // 获取全局唯一实例（单例模式）
    static Config& instance();
    // 加载 JSON 配置文件，成功返回 true
    bool load(const std::string& filename);
    // 获取整数配置值，支持点号路径，不存在时返回默认值
    int getInt(const std::string& path, int defaultVal = 0) const;
    // 获取字符串配置值，支持点号路径，不存在时返回默认值
    std::string getString(const std::string& path, const std::string& defaultVal = "") const;
    // 获取布尔配置值，支持点号路径，不存在时返回默认值
    bool getBool(const std::string& path, bool defaultVal = false) const;

private:
    rapidjson::Document doc_; // 存储解析后的 JSON 文档
    Config() = default;       // 私有构造，确保单例
};