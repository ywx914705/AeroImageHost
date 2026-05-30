-- 06_file_type_index.sql
-- 为文件类型筛选和排序添加复合索引
-- 支持 /api/files?type=image&sort=size&order=desc 等查询

-- 按类型 + 时间排序（默认排序）
ALTER TABLE files ADD INDEX idx_user_type_time (user_id, mime_type, upload_time);
-- 按类型 + 大小排序
ALTER TABLE files ADD INDEX idx_user_type_size (user_id, mime_type, size);
-- 按类型 + 文件名排序
ALTER TABLE files ADD INDEX idx_user_type_name (user_id, mime_type, filename);
