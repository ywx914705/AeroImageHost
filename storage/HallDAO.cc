/*
 * HallDAO.cc - 图床大厅数据访问层实现
 *
 * 职责：封装 hall_posts 和 hall_likes 表的所有 SQL 操作。
 *       通过 ConnectionPool 获取 MySQL 连接，使用 prepared statement 防止 SQL 注入。
 */
#include "HallDAO.hpp"
#include "ConnectionPool.hpp"
#include "Log.hpp"
#include <mysql/mysql.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <chrono>

HallDAO& HallDAO::instance() {
    static HallDAO inst;
    return inst;
}

static MYSQL* getConn() {
    return ConnectionPool::getInstance().getConnection();
}

static void releaseConn(MYSQL* conn) {
    ConnectionPool::getInstance().releaseConnection(conn);
}

// ===================== publish =====================
bool HallDAO::publish(const HallPost& post) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    long long now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    const char* sql = "INSERT INTO hall_posts (file_id, user_id, title, description, tags, created_at) VALUES (?, ?, ?, ?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return false;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    MYSQL_BIND bind[6];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)post.file_id.c_str();
    bind[0].buffer_length = post.file_id.size();
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (void*)&post.user_id;
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)post.title.c_str();
    bind[2].buffer_length = post.title.size();
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)post.description.c_str();
    bind[3].buffer_length = post.description.size();
    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer = (char*)post.tags.c_str();
    bind[4].buffer_length = post.tags.size();
    bind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[5].buffer = &now;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    int ret = mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return (ret == 0);
}

// ===================== listPosts =====================
std::pair<std::vector<HallPost>, int> HallDAO::listPosts(int offset, int limit, const std::string& sort, const std::string& tag, int viewer_user_id) {
    std::vector<HallPost> result;
    int total = 0;
    MYSQL* conn = getConn();
    if (!conn) return {result, total};

    // 白名单校验排序字段
    std::string orderField = "hp.created_at";
    std::string orderDir = "DESC";
    if (sort == "popular") {
        orderField = "hp.likes";
        orderDir = "DESC";
    } else if (sort == "views") {
        orderField = "hp.views";
        orderDir = "DESC";
    }
    // default: "latest" -> hp.created_at DESC

    // 构建 WHERE 子句
    std::string whereClause;
    std::vector<std::string> conditions;
    if (!tag.empty()) {
        conditions.push_back("FIND_IN_SET(?, hp.tags)");
    }

    for (const auto& c : conditions) {
        if (whereClause.empty()) {
            whereClause = "WHERE " + c;
        } else {
            whereClause += " AND " + c;
        }
    }

    // COUNT 总数
    std::string countSql = "SELECT COUNT(*) FROM hall_posts hp " + whereClause;
    MYSQL_STMT* countStmt = mysql_stmt_init(conn);
    if (countStmt && mysql_stmt_prepare(countStmt, countSql.c_str(), countSql.length()) == 0) {
        MYSQL_BIND param;
        memset(&param, 0, sizeof(param));
        if (!tag.empty()) {
            param.buffer_type = MYSQL_TYPE_STRING;
            param.buffer = (char*)tag.c_str();
            param.buffer_length = tag.size();
            mysql_stmt_bind_param(countStmt, &param);
        }
        if (mysql_stmt_execute(countStmt) == 0) {
            MYSQL_BIND resultBind;
            memset(&resultBind, 0, sizeof(resultBind));
            resultBind.buffer_type = MYSQL_TYPE_LONG;
            resultBind.buffer = &total;
            mysql_stmt_bind_result(countStmt, &resultBind);
            mysql_stmt_store_result(countStmt);
            mysql_stmt_fetch(countStmt);
        }
    }
    if (countStmt) mysql_stmt_close(countStmt);

    if (total == 0) {
        releaseConn(conn);
        return {result, total};
    }

    // 主查询：JOIN files 和 users
    std::string sql = "SELECT hp.id, hp.file_id, hp.user_id, hp.title, hp.description, hp.tags, "
                      "hp.likes, hp.views, hp.created_at, "
                      "f.filename, f.mime_type, f.size, "
                      "u.account "
                      "FROM hall_posts hp "
                      "LEFT JOIN files f ON hp.file_id = f.file_id "
                      "LEFT JOIN users u ON hp.user_id = u.id "
                      + whereClause + " ORDER BY " + orderField + " " + orderDir + " LIMIT ?, ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return {result, total};
    }
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return {result, total};
    }

    // 绑定参数
    int paramCount = 2; // offset + limit
    if (!tag.empty()) paramCount = 3; // tag + offset + limit
    std::vector<MYSQL_BIND> params(paramCount);
    memset(params.data(), 0, params.size() * sizeof(MYSQL_BIND));

    int idx = 0;
    if (!tag.empty()) {
        params[idx].buffer_type = MYSQL_TYPE_STRING;
        params[idx].buffer = (char*)tag.c_str();
        params[idx].buffer_length = tag.size();
        idx++;
    }
    params[idx].buffer_type = MYSQL_TYPE_LONG;
    params[idx].buffer = &offset;
    idx++;
    params[idx].buffer_type = MYSQL_TYPE_LONG;
    params[idx].buffer = &limit;

    mysql_stmt_bind_param(stmt, params.data());

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return {result, total};
    }

    // 绑定结果
    long long post_id = 0;
    char file_id_buf[64] = {0};
    int user_id = 0;
    char title_buf[256] = {0};
    char desc_buf[1024] = {0};
    char tags_buf[512] = {0};
    int likes = 0, views = 0;
    long long created_at = 0;
    char filename_buf[512] = {0};
    char mime_buf[256] = {0};
    long long file_size = 0;
    char username_buf[128] = {0};

    bool file_id_null = false, title_null = false, desc_null = false, tags_null = false;
    bool filename_null = false, mime_null = false, username_null = false;
    unsigned long file_id_len = 0, title_len = 0, desc_len = 0, tags_len = 0;
    unsigned long filename_len = 0, mime_len = 0, username_len = 0;

    MYSQL_BIND resultBind[13];
    memset(resultBind, 0, sizeof(resultBind));

    bool post_id_null = false;
    bool likes_null = false, views_null = false, created_at_null = false, file_size_null = false;
    unsigned long post_id_len = 0, user_id_len = 0, likes_len = 0, views_len = 0, created_at_len = 0, file_size_len = 0;

    resultBind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resultBind[0].buffer = &post_id;
    resultBind[0].is_null = &post_id_null;
    resultBind[0].length = &post_id_len;

    resultBind[1].buffer_type = MYSQL_TYPE_STRING;
    resultBind[1].buffer = file_id_buf;
    resultBind[1].buffer_length = sizeof(file_id_buf);
    resultBind[1].is_null = &file_id_null;
    resultBind[1].length = &file_id_len;

    resultBind[2].buffer_type = MYSQL_TYPE_LONG;
    resultBind[2].buffer = &user_id;
    resultBind[2].is_null = &post_id_null;
    resultBind[2].length = &user_id_len;

    resultBind[3].buffer_type = MYSQL_TYPE_STRING;
    resultBind[3].buffer = title_buf;
    resultBind[3].buffer_length = sizeof(title_buf);
    resultBind[3].is_null = &title_null;
    resultBind[3].length = &title_len;

    resultBind[4].buffer_type = MYSQL_TYPE_STRING;
    resultBind[4].buffer = desc_buf;
    resultBind[4].buffer_length = sizeof(desc_buf);
    resultBind[4].is_null = &desc_null;
    resultBind[4].length = &desc_len;

    resultBind[5].buffer_type = MYSQL_TYPE_STRING;
    resultBind[5].buffer = tags_buf;
    resultBind[5].buffer_length = sizeof(tags_buf);
    resultBind[5].is_null = &tags_null;
    resultBind[5].length = &tags_len;

    resultBind[6].buffer_type = MYSQL_TYPE_LONG;
    resultBind[6].buffer = &likes;
    resultBind[6].is_null = &likes_null;
    resultBind[6].length = &likes_len;

    resultBind[7].buffer_type = MYSQL_TYPE_LONG;
    resultBind[7].buffer = &views;
    resultBind[7].is_null = &views_null;
    resultBind[7].length = &views_len;

    resultBind[8].buffer_type = MYSQL_TYPE_LONGLONG;
    resultBind[8].buffer = &created_at;
    resultBind[8].is_null = &created_at_null;
    resultBind[8].length = &created_at_len;

    resultBind[9].buffer_type = MYSQL_TYPE_STRING;
    resultBind[9].buffer = filename_buf;
    resultBind[9].buffer_length = sizeof(filename_buf);
    resultBind[9].is_null = &filename_null;
    resultBind[9].length = &filename_len;

    resultBind[10].buffer_type = MYSQL_TYPE_STRING;
    resultBind[10].buffer = mime_buf;
    resultBind[10].buffer_length = sizeof(mime_buf);
    resultBind[10].is_null = &mime_null;
    resultBind[10].length = &mime_len;

    resultBind[11].buffer_type = MYSQL_TYPE_LONGLONG;
    resultBind[11].buffer = &file_size;
    resultBind[11].is_null = &file_size_null;
    resultBind[11].length = &file_size_len;

    resultBind[12].buffer_type = MYSQL_TYPE_STRING;
    resultBind[12].buffer = username_buf;
    resultBind[12].buffer_length = sizeof(username_buf);
    resultBind[12].is_null = &username_null;
    resultBind[12].length = &username_len;

    mysql_stmt_bind_result(stmt, resultBind);
    mysql_stmt_store_result(stmt);

    while (mysql_stmt_fetch(stmt) == 0) {
        HallPost post;
        post.id = post_id;
        if (!file_id_null) post.file_id = file_id_buf;
        post.user_id = user_id;
        if (!title_null) post.title = title_buf;
        if (!desc_null) post.description = desc_buf;
        if (!tags_null) post.tags = tags_buf;
        post.likes = likes;
        post.views = views;
        post.created_at = created_at;
        if (!filename_null) post.filename = filename_buf;
        if (!mime_null) post.mime_type = mime_buf;
        post.file_size = file_size;
        if (!username_null) post.username = username_buf;
        result.push_back(post);
    }

    mysql_stmt_close(stmt);

    // 如果有 viewer_user_id，检查每个帖子的点赞状态
    if (viewer_user_id > 0 && !result.empty()) {
        std::string likeSql = "SELECT post_id FROM hall_likes WHERE user_id = ? AND post_id IN (";
        std::vector<MYSQL_BIND> likeParams;
        likeParams.push_back({});
        likeParams[0].buffer_type = MYSQL_TYPE_LONG;
        likeParams[0].buffer = &viewer_user_id;

        for (size_t i = 0; i < result.size(); ++i) {
            if (i > 0) likeSql += ",";
            likeSql += "?";
            likeParams.push_back({});
            likeParams[i + 1].buffer_type = MYSQL_TYPE_LONGLONG;
            likeParams[i + 1].buffer = &result[i].id;
        }
        likeSql += ")";

        MYSQL_STMT* likeStmt = mysql_stmt_init(conn);
        if (likeStmt && mysql_stmt_prepare(likeStmt, likeSql.c_str(), likeSql.length()) == 0) {
            mysql_stmt_bind_param(likeStmt, likeParams.data());
            if (mysql_stmt_execute(likeStmt) == 0) {
                long long likedPostId = 0;
                MYSQL_BIND likedBind;
                memset(&likedBind, 0, sizeof(likedBind));
                likedBind.buffer_type = MYSQL_TYPE_LONGLONG;
                likedBind.buffer = &likedPostId;
                mysql_stmt_bind_result(likeStmt, &likedBind);
                mysql_stmt_store_result(likeStmt);
                while (mysql_stmt_fetch(likeStmt) == 0) {
                    for (auto& p : result) {
                        if (p.id == likedPostId) {
                            p.is_liked = true;
                            break;
                        }
                    }
                }
            }
        }
        if (likeStmt) mysql_stmt_close(likeStmt);
    }

    releaseConn(conn);
    return {result, total};
}

// ===================== getPost =====================
HallPost HallDAO::getPost(long long post_id, int viewer_user_id) {
    HallPost post;
    MYSQL* conn = getConn();
    if (!conn) return post;

    const char* sql = "SELECT hp.id, hp.file_id, hp.user_id, hp.title, hp.description, hp.tags, "
                      "hp.likes, hp.views, hp.created_at, "
                      "f.filename, f.mime_type, f.size, "
                      "u.account "
                      "FROM hall_posts hp "
                      "LEFT JOIN files f ON hp.file_id = f.file_id "
                      "LEFT JOIN users u ON hp.user_id = u.id "
                      "WHERE hp.id = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return post;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return post;
    }

    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    param.buffer_type = MYSQL_TYPE_LONGLONG;
    param.buffer = &post_id;
    mysql_stmt_bind_param(stmt, &param);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return post;
    }

    long long id = 0;
    char file_id_buf[64] = {0};
    int user_id = 0;
    char title_buf[256] = {0};
    char desc_buf[1024] = {0};
    char tags_buf[512] = {0};
    int likes = 0, views = 0;
    long long created_at = 0;
    char filename_buf[512] = {0};
    char mime_buf[256] = {0};
    long long file_size = 0;
    char username_buf[128] = {0};

    bool file_id_null = false, title_null = false, desc_null = false, tags_null = false;
    bool filename_null = false, mime_null = false, username_null = false;
    unsigned long file_id_len = 0, title_len = 0, desc_len = 0, tags_len = 0;
    unsigned long filename_len = 0, mime_len = 0, username_len = 0;

    MYSQL_BIND resultBind[13];
    memset(resultBind, 0, sizeof(resultBind));

    resultBind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    resultBind[0].buffer = &id;
    resultBind[1].buffer_type = MYSQL_TYPE_STRING;
    resultBind[1].buffer = file_id_buf;
    resultBind[1].buffer_length = sizeof(file_id_buf);
    resultBind[1].is_null = &file_id_null;
    resultBind[1].length = &file_id_len;
    resultBind[2].buffer_type = MYSQL_TYPE_LONG;
    resultBind[2].buffer = &user_id;
    resultBind[3].buffer_type = MYSQL_TYPE_STRING;
    resultBind[3].buffer = title_buf;
    resultBind[3].buffer_length = sizeof(title_buf);
    resultBind[3].is_null = &title_null;
    resultBind[3].length = &title_len;
    resultBind[4].buffer_type = MYSQL_TYPE_STRING;
    resultBind[4].buffer = desc_buf;
    resultBind[4].buffer_length = sizeof(desc_buf);
    resultBind[4].is_null = &desc_null;
    resultBind[4].length = &desc_len;
    resultBind[5].buffer_type = MYSQL_TYPE_STRING;
    resultBind[5].buffer = tags_buf;
    resultBind[5].buffer_length = sizeof(tags_buf);
    resultBind[5].is_null = &tags_null;
    resultBind[5].length = &tags_len;
    resultBind[6].buffer_type = MYSQL_TYPE_LONG;
    resultBind[6].buffer = &likes;
    resultBind[7].buffer_type = MYSQL_TYPE_LONG;
    resultBind[7].buffer = &views;
    resultBind[8].buffer_type = MYSQL_TYPE_LONGLONG;
    resultBind[8].buffer = &created_at;
    resultBind[9].buffer_type = MYSQL_TYPE_STRING;
    resultBind[9].buffer = filename_buf;
    resultBind[9].buffer_length = sizeof(filename_buf);
    resultBind[9].is_null = &filename_null;
    resultBind[9].length = &filename_len;
    resultBind[10].buffer_type = MYSQL_TYPE_STRING;
    resultBind[10].buffer = mime_buf;
    resultBind[10].buffer_length = sizeof(mime_buf);
    resultBind[10].is_null = &mime_null;
    resultBind[10].length = &mime_len;
    resultBind[11].buffer_type = MYSQL_TYPE_LONGLONG;
    resultBind[11].buffer = &file_size;
    resultBind[12].buffer_type = MYSQL_TYPE_STRING;
    resultBind[12].buffer = username_buf;
    resultBind[12].buffer_length = sizeof(username_buf);
    resultBind[12].is_null = &username_null;
    resultBind[12].length = &username_len;

    mysql_stmt_bind_result(stmt, resultBind);
    mysql_stmt_store_result(stmt);

    if (mysql_stmt_fetch(stmt) == 0) {
        post.id = id;
        if (!file_id_null) post.file_id = file_id_buf;
        post.user_id = user_id;
        if (!title_null) post.title = title_buf;
        if (!desc_null) post.description = desc_buf;
        if (!tags_null) post.tags = tags_buf;
        post.likes = likes;
        post.views = views;
        post.created_at = created_at;
        if (!filename_null) post.filename = filename_buf;
        if (!mime_null) post.mime_type = mime_buf;
        post.file_size = file_size;
        if (!username_null) post.username = username_buf;
    }

    mysql_stmt_close(stmt);

    // 检查当前用户是否已点赞
    if (viewer_user_id > 0 && post.id > 0) {
        const char* likeSql = "SELECT 1 FROM hall_likes WHERE post_id = ? AND user_id = ? LIMIT 1";
        MYSQL_STMT* likeStmt = mysql_stmt_init(conn);
        if (likeStmt && mysql_stmt_prepare(likeStmt, likeSql, strlen(likeSql)) == 0) {
            MYSQL_BIND likeParams[2];
            memset(likeParams, 0, sizeof(likeParams));
            likeParams[0].buffer_type = MYSQL_TYPE_LONGLONG;
            likeParams[0].buffer = &post.id;
            likeParams[1].buffer_type = MYSQL_TYPE_LONG;
            likeParams[1].buffer = &viewer_user_id;
            mysql_stmt_bind_param(likeStmt, likeParams);
            if (mysql_stmt_execute(likeStmt) == 0) {
                int likeExists = 0;
                MYSQL_BIND likeResult;
                memset(&likeResult, 0, sizeof(likeResult));
                likeResult.buffer_type = MYSQL_TYPE_LONG;
                likeResult.buffer = &likeExists;
                mysql_stmt_bind_result(likeStmt, &likeResult);
                mysql_stmt_store_result(likeStmt);
                if (mysql_stmt_fetch(likeStmt) == 0) {
                    post.is_liked = (likeExists != 0);
                }
            }
        }
        if (likeStmt) mysql_stmt_close(likeStmt);
    }

    releaseConn(conn);
    return post;
}

// ===================== deletePost =====================
bool HallDAO::deletePost(long long post_id, int user_id) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    // 先删除关联的点赞记录
    const char* delLikesSql = "DELETE FROM hall_likes WHERE post_id = ?";
    MYSQL_STMT* delLikesStmt = mysql_stmt_init(conn);
    if (delLikesStmt && mysql_stmt_prepare(delLikesStmt, delLikesSql, strlen(delLikesSql)) == 0) {
        MYSQL_BIND param;
        memset(&param, 0, sizeof(param));
        param.buffer_type = MYSQL_TYPE_LONGLONG;
        param.buffer = &post_id;
        mysql_stmt_bind_param(delLikesStmt, &param);
        mysql_stmt_execute(delLikesStmt);
    }
    if (delLikesStmt) mysql_stmt_close(delLikesStmt);

    // 删除帖子（同时验证 user_id 归属）
    const char* sql = "DELETE FROM hall_posts WHERE id = ? AND user_id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return false;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    MYSQL_BIND params[2];
    memset(params, 0, sizeof(params));
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &post_id;
    params[1].buffer_type = MYSQL_TYPE_LONG;
    params[1].buffer = &user_id;
    mysql_stmt_bind_param(stmt, params);

    int ret = mysql_stmt_execute(stmt);
    bool success = (ret == 0 && mysql_stmt_affected_rows(stmt) > 0);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return success;
}

// ===================== toggleLike =====================
bool HallDAO::toggleLike(long long post_id, int user_id) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    // 先检查是否已点赞
    const char* checkSql = "SELECT id FROM hall_likes WHERE post_id = ? AND user_id = ?";
    MYSQL_STMT* checkStmt = mysql_stmt_init(conn);
    if (!checkStmt) {
        releaseConn(conn);
        return false;
    }
    if (mysql_stmt_prepare(checkStmt, checkSql, strlen(checkSql)) != 0) {
        mysql_stmt_close(checkStmt);
        releaseConn(conn);
        return false;
    }

    MYSQL_BIND checkParams[2];
    memset(checkParams, 0, sizeof(checkParams));
    checkParams[0].buffer_type = MYSQL_TYPE_LONGLONG;
    checkParams[0].buffer = &post_id;
    checkParams[1].buffer_type = MYSQL_TYPE_LONG;
    checkParams[1].buffer = &user_id;
    mysql_stmt_bind_param(checkStmt, checkParams);

    bool alreadyLiked = false;
    if (mysql_stmt_execute(checkStmt) == 0) {
        mysql_stmt_store_result(checkStmt);
        alreadyLiked = (mysql_stmt_num_rows(checkStmt) > 0);
    }
    mysql_stmt_close(checkStmt);

    if (alreadyLiked) {
        // 取消点赞：删除记录，likes - 1
        const char* delSql = "DELETE FROM hall_likes WHERE post_id = ? AND user_id = ?";
        MYSQL_STMT* delStmt = mysql_stmt_init(conn);
        if (!delStmt) { releaseConn(conn); return false; }
        if (mysql_stmt_prepare(delStmt, delSql, strlen(delSql)) != 0) {
            mysql_stmt_close(delStmt); releaseConn(conn); return false;
        }
        MYSQL_BIND delParams[2];
        memset(delParams, 0, sizeof(delParams));
        delParams[0].buffer_type = MYSQL_TYPE_LONGLONG;
        delParams[0].buffer = &post_id;
        delParams[1].buffer_type = MYSQL_TYPE_LONG;
        delParams[1].buffer = &user_id;
        mysql_stmt_bind_param(delStmt, delParams);
        int ret = mysql_stmt_execute(delStmt);
        mysql_stmt_close(delStmt);

        if (ret == 0) {
            const char* decSql = "UPDATE hall_posts SET likes = likes - 1 WHERE id = ? AND likes > 0";
            MYSQL_STMT* decStmt = mysql_stmt_init(conn);
            if (decStmt && mysql_stmt_prepare(decStmt, decSql, strlen(decSql)) == 0) {
                MYSQL_BIND decParam;
                memset(&decParam, 0, sizeof(decParam));
                decParam.buffer_type = MYSQL_TYPE_LONGLONG;
                decParam.buffer = &post_id;
                mysql_stmt_bind_param(decStmt, &decParam);
                mysql_stmt_execute(decStmt);
                mysql_stmt_close(decStmt);
            }
        }
        releaseConn(conn);
        return (ret == 0);
    } else {
        // 点赞：插入记录，likes + 1
        long long likeNow = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const char* insSql = "INSERT INTO hall_likes (post_id, user_id, created_at) VALUES (?, ?, ?)";
        MYSQL_STMT* insStmt = mysql_stmt_init(conn);
        if (!insStmt) { releaseConn(conn); return false; }
        if (mysql_stmt_prepare(insStmt, insSql, strlen(insSql)) != 0) {
            mysql_stmt_close(insStmt); releaseConn(conn); return false;
        }
        MYSQL_BIND insParams[3];
        memset(insParams, 0, sizeof(insParams));
        insParams[0].buffer_type = MYSQL_TYPE_LONGLONG;
        insParams[0].buffer = &post_id;
        insParams[1].buffer_type = MYSQL_TYPE_LONG;
        insParams[1].buffer = &user_id;
        insParams[2].buffer_type = MYSQL_TYPE_LONGLONG;
        insParams[2].buffer = &likeNow;
        mysql_stmt_bind_param(insStmt, insParams);
        int ret = mysql_stmt_execute(insStmt);
        mysql_stmt_close(insStmt);

        if (ret == 0) {
            const char* incSql = "UPDATE hall_posts SET likes = likes + 1 WHERE id = ?";
            MYSQL_STMT* incStmt = mysql_stmt_init(conn);
            if (incStmt && mysql_stmt_prepare(incStmt, incSql, strlen(incSql)) == 0) {
                MYSQL_BIND incParam;
                memset(&incParam, 0, sizeof(incParam));
                incParam.buffer_type = MYSQL_TYPE_LONGLONG;
                incParam.buffer = &post_id;
                mysql_stmt_bind_param(incStmt, &incParam);
                mysql_stmt_execute(incStmt);
                mysql_stmt_close(incStmt);
            }
        }
        releaseConn(conn);
        return (ret == 0);
    }
}

// ===================== incrementViews =====================
bool HallDAO::incrementViews(long long post_id) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "UPDATE hall_posts SET views = views + 1 WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return false;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    param.buffer_type = MYSQL_TYPE_LONGLONG;
    param.buffer = &post_id;
    mysql_stmt_bind_param(stmt, &param);

    int ret = mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return (ret == 0);
}

// ===================== isPublished =====================
bool HallDAO::isPublished(const std::string& file_id) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "SELECT 1 FROM hall_posts WHERE file_id = ? LIMIT 1";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return false;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    MYSQL_BIND param;
    memset(&param, 0, sizeof(param));
    param.buffer_type = MYSQL_TYPE_STRING;
    param.buffer = (char*)file_id.c_str();
    param.buffer_length = file_id.size();
    mysql_stmt_bind_param(stmt, &param);

    bool found = false;
    if (mysql_stmt_execute(stmt) == 0) {
        mysql_stmt_store_result(stmt);
        found = (mysql_stmt_num_rows(stmt) > 0);
    }
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return found;
}

// ===================== getTags =====================
std::vector<std::pair<std::string, int>> HallDAO::getTags() {
    std::vector<std::pair<std::string, int>> result;
    MYSQL* conn = getConn();
    if (!conn) return result;

    // 获取所有帖子的 tags 字段
    const char* sql = "SELECT tags FROM hall_posts WHERE tags IS NOT NULL AND tags != ''";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return result;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return result;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return result;
    }

    char tags_buf[4096] = {0};
    bool tags_null = false;
    unsigned long tags_len = 0;
    MYSQL_BIND resultBind;
    memset(&resultBind, 0, sizeof(resultBind));
    resultBind.buffer_type = MYSQL_TYPE_STRING;
    resultBind.buffer = tags_buf;
    resultBind.buffer_length = sizeof(tags_buf);
    resultBind.is_null = &tags_null;
    resultBind.length = &tags_len;
    mysql_stmt_bind_result(stmt, &resultBind);
    mysql_stmt_store_result(stmt);

    // 统计每个标签的使用次数
    std::map<std::string, int> tagCount;
    while (mysql_stmt_fetch(stmt) == 0) {
        if (tags_null || tags_buf[0] == '\0') continue;
        std::string tagsStr(tags_buf, tags_len);
        std::istringstream ss(tagsStr);
        std::string tag;
        while (std::getline(ss, tag, ',')) {
            // 去除首尾空格
            size_t start = tag.find_first_not_of(" \t\n\r");
            size_t end = tag.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
                std::string trimmed = tag.substr(start, end - start + 1);
                if (!trimmed.empty()) {
                    tagCount[trimmed]++;
                }
            }
        }
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);

    // 按使用次数降序排列
    for (auto& [tag, count] : tagCount) {
        result.emplace_back(tag, count);
    }
    std::sort(result.begin(), result.end(),
              [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                  return a.second > b.second;
              });

    return result;
}
