/*
 * FolderDAO.hpp - 文件夹数据访问层头文件
 *
 * 职责：封装 folders 表的所有 SQL 操作（CRUD + 树形查询 + 路径查询）。
 *       通过 ConnectionPool 获取 MySQL 连接，使用 prepared statement 防止 SQL 注入。
 */
#pragma once
#include <string>
#include <vector>

struct Folder {
    long long id = 0;
    int user_id = 0;
    long long parent_id = 0;
    std::string name;
    std::string icon;
    std::string color;
    int sort_order = 0;
    long long created_at = 0;
    long long updated_at = 0;
    int file_count = 0;   // 文件数量（查询时填充）
    int child_count = 0;  // 子文件夹数量
};

class FolderDAO {
public:
    static FolderDAO& instance();

    // 创建文件夹
    long long create(int user_id, long long parent_id, const std::string& name);
    // 获取用户的文件夹列表（平铺）
    std::vector<Folder> listByUser(int user_id, long long parent_id);
    // 获取单个文件夹
    Folder get(long long folder_id, int user_id);
    // 重命名
    bool rename(long long folder_id, int user_id, const std::string& newName);
    // 删除（级联）
    bool deleteFolder(long long folder_id, int user_id);
    // 移动
    bool move(long long folder_id, int user_id, long long newParentId);
    // 获取子文件夹
    std::vector<Folder> getChildren(long long parentId, int user_id);
    // 获取文件夹路径（从根到当前）
    std::vector<Folder> getPath(long long folderId, int user_id);
    // 检查是否是子文件夹（防止循环）
    bool isDescendant(long long ancestorId, long long descendantId, int user_id);
    // 获取文件夹深度（从根到该文件夹的层数，根为0）
    int getDepth(long long folder_id, int user_id);
    // 获取指定文件夹下的文件数量
    int getFileCount(long long folder_id, int user_id);
    // 获取指定文件夹下的子文件夹数量
    int getChildCount(long long folder_id, int user_id);
    // 更新文件夹排序
    bool updateSort(long long folder_id, int user_id, int sort_order);
    // 更新文件夹图标和颜色
    bool updateAppearance(long long folder_id, int user_id, const std::string& icon, const std::string& color);
};
