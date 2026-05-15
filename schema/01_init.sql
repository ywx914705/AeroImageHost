-- AeroImageHost 数据库初始化脚本
-- 用于初始化数据库表结构

-- 创建数据库（如果不存在）
CREATE DATABASE IF NOT EXISTS imagehost DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
USE imagehost;

-- 用户表
CREATE TABLE IF NOT EXISTS users (
    id INT NOT NULL AUTO_INCREMENT,
    account VARCHAR(64) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    email VARCHAR(255) DEFAULT NULL,
    created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY account (account),
    UNIQUE KEY email (email)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- 文件元数据表
CREATE TABLE IF NOT EXISTS files (
    id BIGINT NOT NULL AUTO_INCREMENT,
    file_id VARCHAR(64) NOT NULL,
    user_id INT NOT NULL,
    filename VARCHAR(512) NOT NULL,
    size BIGINT NOT NULL DEFAULT 0,
    mime_type VARCHAR(256) DEFAULT NULL,
    width INT DEFAULT 0,
    height INT DEFAULT 0,
    upload_time BIGINT NOT NULL,
    is_public TINYINT(1) DEFAULT 0,
    view_count BIGINT DEFAULT 0,
    allow_domains VARCHAR(512) DEFAULT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY file_id (file_id),
    KEY idx_user_upload (user_id, upload_time),
    KEY upload_time (upload_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
