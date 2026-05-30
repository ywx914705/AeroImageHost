/*
 * FolderDAO.cc - 文件夹数据访问层实现
 *
 * 职责：封装 folders 表的所有 SQL 操作（CRUD + 树形查询 + 路径查询）。
 *       通过 ConnectionPool 获取 MySQL 连接，使用 prepared statement 防止 SQL 注入。
 */
#include "FolderDAO.hpp"
#include "ConnectionPool.hpp"
#include "Log.hpp"
#include <mysql/mysql.h>
#include <cstring>
#include <ctime>
#include <algorithm>

FolderDAO& FolderDAO::instance() {
    static FolderDAO inst;
    return inst;
}

static MYSQL* getConn() {
    return ConnectionPool::getInstance().getConnection();
}

static void releaseConn(MYSQL* conn) {
    ConnectionPool::getInstance().releaseConnection(conn);
}

// 辅助函数：执行带参数绑定的查询并返回是否成功
static bool executeParamStmt(MYSQL* conn, const char* sql, MYSQL_BIND* params, int paramCount) {
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) return false;
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }
    if (paramCount > 0 && mysql_stmt_bind_param(stmt, params) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }
    bool ok = (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return ok;
}

// 辅助函数：读取 Folder 结果行
static bool fetchFolderRow(MYSQL_STMT* stmt, Folder& folder) {
    char name_buf[256] = {0};
    char icon_buf[50] = {0};
    char color_buf[8] = {0};
    long long id = 0, parent_id = 0, created_at = 0, updated_at = 0;
    int user_id = 0, sort_order = 0;
    bool parent_id_null = false;

    MYSQL_BIND result[9];
    memset(result, 0, sizeof(result));

    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &id;
    result[1].buffer_type = MYSQL_TYPE_LONG;
    result[1].buffer = &user_id;
    result[2].buffer_type = MYSQL_TYPE_LONGLONG;
    result[2].buffer = &parent_id;
    result[2].is_null = &parent_id_null;
    result[3].buffer_type = MYSQL_TYPE_STRING;
    result[3].buffer = name_buf;
    result[3].buffer_length = sizeof(name_buf);
    result[4].buffer_type = MYSQL_TYPE_STRING;
    result[4].buffer = icon_buf;
    result[4].buffer_length = sizeof(icon_buf);
    result[5].buffer_type = MYSQL_TYPE_STRING;
    result[5].buffer = color_buf;
    result[5].buffer_length = sizeof(color_buf);
    result[6].buffer_type = MYSQL_TYPE_LONG;
    result[6].buffer = &sort_order;
    result[7].buffer_type = MYSQL_TYPE_LONGLONG;
    result[7].buffer = &created_at;
    result[8].buffer_type = MYSQL_TYPE_LONGLONG;
    result[8].buffer = &updated_at;

    bool name_null = false, icon_null = false, color_null = false;
    result[3].is_null = &name_null;
    result[4].is_null = &icon_null;
    result[5].is_null = &color_null;

    unsigned long name_len = 0, icon_len = 0, color_len = 0;
    result[3].length = &name_len;
    result[4].length = &icon_len;
    result[5].length = &color_len;

    mysql_stmt_bind_result(stmt, result);
    mysql_stmt_store_result(stmt);

    if (mysql_stmt_fetch(stmt) != 0) return false;

    folder.id = id;
    folder.user_id = user_id;
    folder.parent_id = parent_id_null ? 0 : parent_id;
    if (!name_null) folder.name = std::string(name_buf, name_len);
    if (!icon_null) folder.icon = std::string(icon_buf, icon_len);
    if (!color_null) folder.color = std::string(color_buf, color_len);
    folder.sort_order = sort_order;
    folder.created_at = created_at;
    folder.updated_at = updated_at;
    return true;
}

// 创建文件夹
long long FolderDAO::create(int user_id, long long parent_id, const std::string& name) {
    MYSQL* conn = getConn();
    if (!conn) return -1;

    long long now = static_cast<long long>(std::time(nullptr));

    // 如果有父文件夹，验证父文件夹存在且属于该用户
    if (parent_id > 0) {
        Folder parent = get(parent_id, user_id);
        if (parent.id == 0) {
            releaseConn(conn);
            return -1;
        }
    }

    const char* sql = "INSERT INTO folders (user_id, parent_id, name, created_at, updated_at) VALUES (?, ?, ?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return -1;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return -1;
    }

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &user_id;
    long long pid = parent_id;
    bool pid_null = (parent_id <= 0);
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = &pid;
    bind[1].is_null = &pid_null;
    std::string nameCopy = name;
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = &nameCopy[0];
    bind[2].buffer_length = nameCopy.size();
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = &now;
    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = &now;

    if (mysql_stmt_bind_param(stmt, bind) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return -1;
    }

    int ret = mysql_stmt_execute(stmt);
    long long inserted_id = -1;
    if (ret == 0) {
        inserted_id = static_cast<long long>(mysql_stmt_insert_id(stmt));
    }
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return inserted_id;
}

// 获取用户的文件夹列表（平铺，按 sort_order 排序）
std::vector<Folder> FolderDAO::listByUser(int user_id, long long parent_id) {
    std::vector<Folder> result;
    MYSQL* conn = getConn();
    if (!conn) return result;

    // 当 parent_id=0 时查询根文件夹（parent_id IS NULL），否则查询指定父文件夹
    std::string sql;
    if (parent_id <= 0) {
        sql = "SELECT id, user_id, parent_id, name, icon, color, sort_order, created_at, updated_at "
              "FROM folders WHERE user_id = ? AND parent_id IS NULL "
              "ORDER BY sort_order ASC, name ASC";
    } else {
        sql = "SELECT id, user_id, parent_id, name, icon, color, sort_order, created_at, updated_at "
              "FROM folders WHERE user_id = ? AND parent_id = ? "
              "ORDER BY sort_order ASC, name ASC";
    }

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return result;
    }
    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return result;
    }

    MYSQL_BIND param[2];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].buffer = &user_id;
    param[1].buffer_type = MYSQL_TYPE_LONGLONG;
    param[1].buffer = &parent_id;

    int bindCount = (parent_id <= 0) ? 1 : 2;
    mysql_stmt_bind_param(stmt, param);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return result;
    }

    Folder folder;
    while (fetchFolderRow(stmt, folder)) {
        folder.file_count = getFileCount(folder.id, user_id);
        folder.child_count = getChildCount(folder.id, user_id);
        result.push_back(folder);
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return result;
}

// 获取单个文件夹
Folder FolderDAO::get(long long folder_id, int user_id) {
    Folder folder;
    MYSQL* conn = getConn();
    if (!conn) return folder;

    const char* sql = "SELECT id, user_id, parent_id, name, icon, color, sort_order, created_at, updated_at "
                      "FROM folders WHERE id = ? AND user_id = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return folder;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return folder;
    }

    MYSQL_BIND param[2];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &folder_id;
    param[1].buffer_type = MYSQL_TYPE_LONG;
    param[1].buffer = &user_id;

    mysql_stmt_bind_param(stmt, param);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return folder;
    }

    fetchFolderRow(stmt, folder);

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return folder;
}

// 重命名
bool FolderDAO::rename(long long folder_id, int user_id, const std::string& newName) {
    if (newName.empty() || newName.size() > 255) return false;

    MYSQL* conn = getConn();
    if (!conn) return false;

    long long now = static_cast<long long>(std::time(nullptr));
    const char* sql = "UPDATE folders SET name = ?, updated_at = ? WHERE id = ? AND user_id = ?";

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

    std::string nameCopy = newName;
    MYSQL_BIND param[4];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_STRING;
    param[0].buffer = &nameCopy[0];
    param[0].buffer_length = nameCopy.size();
    param[1].buffer_type = MYSQL_TYPE_LONGLONG;
    param[1].buffer = &now;
    param[2].buffer_type = MYSQL_TYPE_LONGLONG;
    param[2].buffer = &folder_id;
    param[3].buffer_type = MYSQL_TYPE_LONG;
    param[3].buffer = &user_id;

    mysql_stmt_bind_param(stmt, param);

    int ret = mysql_stmt_execute(stmt);
    bool success = (ret == 0 && mysql_stmt_affected_rows(stmt) > 0);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return success;
}

// 删除文件夹（级联由 MySQL ON DELETE CASCADE 处理）
bool FolderDAO::deleteFolder(long long folder_id, int user_id) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    const char* sql = "DELETE FROM folders WHERE id = ? AND user_id = ?";

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

    MYSQL_BIND param[2];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &folder_id;
    param[1].buffer_type = MYSQL_TYPE_LONG;
    param[1].buffer = &user_id;

    mysql_stmt_bind_param(stmt, param);

    int ret = mysql_stmt_execute(stmt);
    bool success = (ret == 0 && mysql_stmt_affected_rows(stmt) > 0);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return success;
}

// 移动文件夹
bool FolderDAO::move(long long folder_id, int user_id, long long newParentId) {
    // 防止循环引用
    if (newParentId > 0 && isDescendant(folder_id, newParentId, user_id)) {
        return false;
    }

    // 验证目标父文件夹属于该用户
    if (newParentId > 0) {
        Folder parent = get(newParentId, user_id);
        if (parent.id == 0) return false;
    }

    MYSQL* conn = getConn();
    if (!conn) return false;

    long long now = static_cast<long long>(std::time(nullptr));
    const char* sql = "UPDATE folders SET parent_id = ?, updated_at = ? WHERE id = ? AND user_id = ?";

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

    MYSQL_BIND param[4];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &newParentId;
    param[1].buffer_type = MYSQL_TYPE_LONGLONG;
    param[1].buffer = &now;
    param[2].buffer_type = MYSQL_TYPE_LONGLONG;
    param[2].buffer = &folder_id;
    param[3].buffer_type = MYSQL_TYPE_LONG;
    param[3].buffer = &user_id;

    mysql_stmt_bind_param(stmt, param);

    int ret = mysql_stmt_execute(stmt);
    bool success = (ret == 0 && mysql_stmt_affected_rows(stmt) > 0);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return success;
}

// 获取子文件夹
std::vector<Folder> FolderDAO::getChildren(long long parentId, int user_id) {
    return listByUser(user_id, parentId);
}

// 获取文件夹路径（从根到当前）
std::vector<Folder> FolderDAO::getPath(long long folderId, int user_id) {
    std::vector<Folder> path;
    long long currentId = folderId;

    while (currentId > 0) {
        Folder f = get(currentId, user_id);
        if (f.id == 0) break;
        path.push_back(f);
        currentId = f.parent_id;
    }

    std::reverse(path.begin(), path.end());
    return path;
}

// 检查是否是子文件夹（ancestor 是否是 descendant 的祖先）
bool FolderDAO::isDescendant(long long ancestorId, long long descendantId, int user_id) {
    if (ancestorId == descendantId) return false;

    MYSQL* conn = getConn();
    if (!conn) return false;

    // 使用递归 CTE 查询祖先链
    const char* sql = "WITH RECURSIVE cte AS ("
                      "  SELECT id, parent_id FROM folders WHERE id = ? AND user_id = ? "
                      "  UNION ALL "
                      "  SELECT f.id, f.parent_id FROM folders f JOIN cte c ON f.parent_id = c.id "
                      ") SELECT COUNT(*) FROM cte WHERE id = ?";

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

    MYSQL_BIND param[3];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &descendantId;
    param[1].buffer_type = MYSQL_TYPE_LONG;
    param[1].buffer = &user_id;
    param[2].buffer_type = MYSQL_TYPE_LONGLONG;
    param[2].buffer = &ancestorId;

    mysql_stmt_bind_param(stmt, param);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return false;
    }

    int count = 0;
    MYSQL_BIND result;
    memset(&result, 0, sizeof(result));
    result.buffer_type = MYSQL_TYPE_LONG;
    result.buffer = &count;
    mysql_stmt_bind_result(stmt, &result);
    mysql_stmt_store_result(stmt);
    mysql_stmt_fetch(stmt);

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return count > 0;
}

// 获取文件夹深度
int FolderDAO::getDepth(long long folder_id, int user_id) {
    auto path = getPath(folder_id, user_id);
    return static_cast<int>(path.size()) - 1; // 根为0
}

// 获取指定文件夹下的文件数量
int FolderDAO::getFileCount(long long folder_id, int user_id) {
    MYSQL* conn = getConn();
    if (!conn) return 0;

    const char* sql = "SELECT COUNT(*) FROM files WHERE folder_id = ? AND user_id = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return 0;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return 0;
    }

    MYSQL_BIND param[2];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &folder_id;
    param[1].buffer_type = MYSQL_TYPE_LONG;
    param[1].buffer = &user_id;

    mysql_stmt_bind_param(stmt, param);

    int count = 0;
    if (mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND result;
        memset(&result, 0, sizeof(result));
        result.buffer_type = MYSQL_TYPE_LONG;
        result.buffer = &count;
        mysql_stmt_bind_result(stmt, &result);
        mysql_stmt_store_result(stmt);
        mysql_stmt_fetch(stmt);
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return count;
}

// 获取指定文件夹下的子文件夹数量
int FolderDAO::getChildCount(long long folder_id, int user_id) {
    MYSQL* conn = getConn();
    if (!conn) return 0;

    const char* sql = "SELECT COUNT(*) FROM folders WHERE parent_id = ? AND user_id = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        releaseConn(conn);
        return 0;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        releaseConn(conn);
        return 0;
    }

    MYSQL_BIND param[2];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    param[0].buffer = &folder_id;
    param[1].buffer_type = MYSQL_TYPE_LONG;
    param[1].buffer = &user_id;

    mysql_stmt_bind_param(stmt, param);

    int count = 0;
    if (mysql_stmt_execute(stmt) == 0) {
        MYSQL_BIND result;
        memset(&result, 0, sizeof(result));
        result.buffer_type = MYSQL_TYPE_LONG;
        result.buffer = &count;
        mysql_stmt_bind_result(stmt, &result);
        mysql_stmt_store_result(stmt);
        mysql_stmt_fetch(stmt);
    }

    mysql_stmt_close(stmt);
    releaseConn(conn);
    return count;
}

// 更新文件夹排序
bool FolderDAO::updateSort(long long folder_id, int user_id, int sort_order) {
    MYSQL* conn = getConn();
    if (!conn) return false;

    long long now = static_cast<long long>(std::time(nullptr));
    const char* sql = "UPDATE folders SET sort_order = ?, updated_at = ? WHERE id = ? AND user_id = ?";

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

    MYSQL_BIND param[4];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].buffer = &sort_order;
    param[1].buffer_type = MYSQL_TYPE_LONGLONG;
    param[1].buffer = &now;
    param[2].buffer_type = MYSQL_TYPE_LONGLONG;
    param[2].buffer = &folder_id;
    param[3].buffer_type = MYSQL_TYPE_LONG;
    param[3].buffer = &user_id;

    mysql_stmt_bind_param(stmt, param);

    int ret = mysql_stmt_execute(stmt);
    bool success = (ret == 0 && mysql_stmt_affected_rows(stmt) > 0);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return success;
}

// 更新文件夹图标和颜色
bool FolderDAO::updateAppearance(long long folder_id, int user_id, const std::string& icon, const std::string& color) {
    if (icon.size() > 50 || color.size() > 7) return false;

    MYSQL* conn = getConn();
    if (!conn) return false;

    long long now = static_cast<long long>(std::time(nullptr));
    const char* sql = "UPDATE folders SET icon = ?, color = ?, updated_at = ? WHERE id = ? AND user_id = ?";

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

    std::string iconCopy = icon;
    std::string colorCopy = color;
    MYSQL_BIND param[5];
    memset(param, 0, sizeof(param));
    param[0].buffer_type = MYSQL_TYPE_STRING;
    param[0].buffer = &iconCopy[0];
    param[0].buffer_length = iconCopy.size();
    param[1].buffer_type = MYSQL_TYPE_STRING;
    param[1].buffer = &colorCopy[0];
    param[1].buffer_length = colorCopy.size();
    param[2].buffer_type = MYSQL_TYPE_LONGLONG;
    param[2].buffer = &now;
    param[3].buffer_type = MYSQL_TYPE_LONGLONG;
    param[3].buffer = &folder_id;
    param[4].buffer_type = MYSQL_TYPE_LONG;
    param[4].buffer = &user_id;

    mysql_stmt_bind_param(stmt, param);

    int ret = mysql_stmt_execute(stmt);
    bool success = (ret == 0 && mysql_stmt_affected_rows(stmt) > 0);
    mysql_stmt_close(stmt);
    releaseConn(conn);
    return success;
}
