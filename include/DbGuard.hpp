/*
 * DbGuard.hpp - 数据库连接 RAII 守卫
 *
 * 职责：自动管理 MySQL 连接的获取和归还，防止连接泄漏。
 *
 * 使用方式：
 *   DbGuard guard;
 *   if (!guard) { /* 连接获取失败 *\/ }
 *   MYSQL* conn = guard.get();
 *   // 使用 conn 执行查询...
 *   // guard 离开作用域时自动归还连接
 */
#pragma once

#include "ConnectionPool.hpp"

class DbGuard {
public:
    DbGuard() : conn_(ConnectionPool::getInstance().getConnection()) {}
    ~DbGuard() {
        if (conn_) {
            ConnectionPool::getInstance().releaseConnection(conn_);
        }
    }

    // 获取原始连接指针
    MYSQL* get() { return conn_; }

    // 检查连接是否有效
    explicit operator bool() const { return conn_ != nullptr; }

    // 禁止拷贝
    DbGuard(const DbGuard&) = delete;
    DbGuard& operator=(const DbGuard&) = delete;

private:
    MYSQL* conn_;
};
