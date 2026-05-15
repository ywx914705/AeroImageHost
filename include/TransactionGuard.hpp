#pragma once

#include <mysql/mysql.h>
#include "ConnectionPool.hpp"

class TransactionGuard {
public:
    explicit TransactionGuard(MYSQL* conn) : conn_(conn), committed_(false) {
        if (conn_) mysql_autocommit(conn_, 0);
    }

    ~TransactionGuard() {
        if (conn_) {
            if (!committed_) mysql_rollback(conn_);
            mysql_autocommit(conn_, 1);
            ConnectionPool::getInstance().releaseConnection(conn_);
        }
    }

    MYSQL* get() { return conn_; }

    void commit() {
        if (conn_) {
            mysql_commit(conn_);
            committed_ = true;
        }
    }

    explicit operator bool() const { return conn_ != nullptr; }

    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;

private:
    MYSQL* conn_;
    bool committed_;
};
