-- AeroImageHost 数据清理脚本
-- 用于清空旧用户数据，解决密码哈希不兼容问题

USE imagehost;

-- 备份现有用户数据（可选）
-- CREATE TABLE users_backup AS SELECT * FROM users;

-- 清空用户数据（因为密码哈希算法已变更，旧数据无法使用）
TRUNCATE TABLE users;

-- 清空文件数据（可选，如果需要保留文件请注释掉）
-- TRUNCATE TABLE files;

-- 验证清理结果
SELECT 'users' as table_name, COUNT(*) as row_count FROM users
UNION ALL
SELECT 'files' as table_name, COUNT(*) as row_count FROM files;
