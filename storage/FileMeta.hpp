/*
 * FileMeta.hpp - 文件元数据模块头文件
 *
 * 职责：
 *   1. 定义 FileMeta 结构体：对应 MySQL files 表的字段
 *   2. 定义 FileMetaDAO 类：文件元数据的 CRUD 操作（数据访问层）
 *
 * 在项目中的作用：
 *   - 上传文件时调用 save() 存储元数据
 *   - 访问文件时调用 get() 获取元数据（用于权限检查、Content-Type 等）
 *   - 文件列表调用 listByUserWithSearch() 分页查询
 *   - 删除文件时调用 del() 清理元数据
 *
 * 设计：FileMetaDAO 是单例，所有数据库操作通过 ConnectionPool 获取 MySQL 连接。
 */
#pragma once
#include <string>
#include <vector>
#include <ctime>
#include "Utils.hpp"

// 文件元数据结构体，对应 MySQL files 表
struct FileMeta {
    std::string file_id;     // UUID 格式的文件唯一标识（同时也是 MinIO 中的 object key）
    int user_id = 0;         // 所属用户 ID
    std::string filename;    // 原始文件名（用户上传时的名称）
    long long size = 0;      // 文件大小（字节）
    std::string mime_type;   // MIME 类型（如 "image/jpeg"、"video/mp4"）
    std::string md5;         // MD5 哈希（预留字段，当前未使用）
    int width = 0;           // 图片宽度（像素，非图片为 0）
    int height = 0;          // 图片高度（像素，非图片为 0）
    time_t upload_time = 0;  // 上传时间（Unix 时间戳）
    bool is_public = false;  // 是否公开（true=无需认证即可访问）
    long long view_count = 0; // 浏览次数
    std::string allow_domains; // 允许访问的域名列表（预留字段，当前未使用）
};

// 文件元数据数据访问对象（DAO），封装所有 MySQL 操作
class FileMetaDAO {
public:
    static FileMetaDAO& instance(); // 获取单例实例

    // 保存文件元数据到数据库（INSERT）
    bool save(const FileMeta& meta);
    // 根据 file_id 获取文件元数据（SELECT）
    FileMeta get(const std::string& file_id);
    // 按用户分页查询文件列表（无搜索条件）
    std::vector<FileMeta> listByUser(int user_id, int offset, int limit);
    // 按用户分页查询文件列表（支持文件名模糊搜索）
    std::vector<FileMeta> listByUserWithSearch(int user_id, const std::string& keyword, int offset, int limit);
    // 统计用户的文件总数（无搜索条件）
    int countByUser(int user_id);
    // 统计用户的文件总数（支持文件名模糊搜索）
    int countByUserWithSearch(int user_id, const std::string& keyword);
    // 根据 file_id 删除文件元数据（DELETE）
    bool del(const std::string& file_id);
    // 切换文件的公开/私有状态
    bool updatePublic(const std::string& file_id, bool is_public);
    // 更新允许访问的域名列表（预留功能）
    bool updateAllowDomains(const std::string& file_id, const std::string& domains);
    // 增加文件浏览次数
    bool incrementViewCount(const std::string& file_id);
    // 批量同步浏览计数（从 Redis 同步到 MySQL）
    bool batchUpdateViewCount(const std::vector<std::pair<std::string, long long>>& updates);
    // 清理 MinIO 中不存在但数据库中仍有记录的孤儿文件
    int cleanupOrphanedFiles();

    // 批量删除：查询属于该用户的有效 file_id 列表
    std::vector<std::string> getValidFileIds(const std::vector<std::string>& file_ids, int user_id);
    // 批量删除：删除指定 file_id 列表的记录
    bool deleteFilesBatch(const std::vector<std::string>& valid_file_ids);
    // 获取所有文件 ID（用于孤儿文件清理）
    std::vector<std::string> getAllFileIds();
    // 获取文件统计信息（总数、图片数、总大小）
    void getFileStats(int& total_files, int& total_images, long long& total_size);
    // 获取用户已使用的存储空间（字节）
    long long getUserStorageUsage(int user_id);
};
