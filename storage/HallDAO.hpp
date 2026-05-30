/*
 * HallDAO.hpp - 图床大厅数据访问层头文件
 *
 * 职责：封装 hall_posts 和 hall_likes 表的所有 SQL 操作（发布、列表、详情、删除、点赞、浏览量、标签统计）。
 *       通过 ConnectionPool 获取 MySQL 连接，使用 prepared statement 防止 SQL 注入。
 */
#pragma once
#include <string>
#include <vector>
#include <map>
#include <utility>

struct HallPost {
    long long id = 0;
    std::string file_id;
    int user_id = 0;
    std::string title;
    std::string description;
    std::string tags;
    int likes = 0;
    int views = 0;
    long long created_at = 0;
    // 额外字段（JOIN 查询时填充）
    std::string filename;
    std::string mime_type;
    long long file_size = 0;
    std::string username;
    bool is_liked = false; // 当前用户是否已点赞
};

class HallDAO {
public:
    static HallDAO& instance();

    // 发布到大厅
    bool publish(const HallPost& post);
    // 获取大厅帖子列表（分页、排序、标签筛选）
    std::pair<std::vector<HallPost>, int> listPosts(int offset, int limit, const std::string& sort = "latest", const std::string& tag = "", int viewer_user_id = 0);
    // 获取单个帖子详情
    HallPost getPost(long long post_id, int viewer_user_id = 0);
    // 删除帖子
    bool deletePost(long long post_id, int user_id);
    // 点赞/取消点赞
    bool toggleLike(long long post_id, int user_id);
    // 增加浏览量
    bool incrementViews(long long post_id);
    // 检查文件是否已发布
    bool isPublished(const std::string& file_id);
    // 获取所有标签
    std::vector<std::pair<std::string, int>> getTags();
};
