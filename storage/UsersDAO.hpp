/*
 * UsersDAO.hpp - 用户数据访问层头文件
 *
 * 职责：封装 users 表的所有 SQL 操作（注册、登录、邮箱检查、统计）。
 *       通过 ConnectionPool 获取 MySQL 连接，使用 prepared statement 防止 SQL 注入。
 *
 * 工作流程：
 *   1. 普通注册 → registerUser() 插入 account + passwordHash
 *   2. 登录 → loginUser() 查询 account 对应的 user_id 和 passwordHash
 *   3. 邮箱注册 → 事务内调用 emailExists() 检查唯一性 → registerUserWithEmail() 插入
 *   4. 统计 → getUserCount() 获取注册用户总数
 */
#pragma once
#include <mysql/mysql.h>
#include <string>

class UsersDAO {
public:
    static UsersDAO& instance();

    // 注册普通用户（account + passwordHash），重复返回 false
    bool registerUser(const std::string& account, const std::string& passwordHash);

    // 登录验证，返回 user_id (>0) / 0(不存在) / -1(错误)
    // passwordHash 通过引用返回数据库中存储的哈希值
    int loginUser(const std::string& account, std::string& passwordHash);

    // 检查邮箱是否已注册（事务内使用，复用传入的连接）
    bool emailExists(MYSQL* conn, const std::string& email);

    // 邮箱注册（事务内使用，复用传入的连接）
    // 返回：0=成功, -1=插入失败(账号/邮箱重复), -2=其他错误
    int registerUserWithEmail(MYSQL* conn, const std::string& account,
                              const std::string& passwordHash, const std::string& email);

    // 获取注册用户总数
    int getUserCount();

private:
    UsersDAO() = default;
    ~UsersDAO() = default;
    UsersDAO(const UsersDAO&) = delete;
    UsersDAO& operator=(const UsersDAO&) = delete;
};
