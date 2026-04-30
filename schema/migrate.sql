-- AeroImageHost 数据库迁移脚本
-- 用于升级现有数据库表结构

USE imagehost;

-- 为 files 表添加 view_count 字段（如果不存在）
ALTER TABLE files ADD COLUMN IF NOT EXISTS view_count BIGINT DEFAULT 0 AFTER is_public;

-- 为 files 表添加索引提升查询性能
CREATE INDEX IF NOT EXISTS idx_files_user_id ON files(user_id);
CREATE INDEX IF NOT EXISTS idx_files_upload_time ON files(upload_time DESC);

-- 为 users 表的 account 字段确保唯一索引
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_account ON users(account);
