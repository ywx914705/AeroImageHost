-- AeroImageHost 数据库迁移脚本
-- 用于升级现有数据库表结构

USE imagehost;

-- 为 files 表添加 view_count 字段（如果不存在）
-- MySQL 8.0.19+ 支持 ADD COLUMN IF NOT EXISTS
ALTER TABLE files ADD COLUMN IF NOT EXISTS view_count BIGINT DEFAULT 0 AFTER is_public;
