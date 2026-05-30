-- 08_folders.sql
-- 文件夹系统

CREATE TABLE IF NOT EXISTS folders (
    id BIGINT NOT NULL AUTO_INCREMENT,
    user_id INT NOT NULL,
    parent_id BIGINT DEFAULT NULL,
    name VARCHAR(255) NOT NULL,
    icon VARCHAR(50) DEFAULT 'folder',
    color VARCHAR(7) DEFAULT '#2563eb',
    sort_order INT DEFAULT 0,
    created_at BIGINT NOT NULL,
    updated_at BIGINT NOT NULL,
    PRIMARY KEY (id),
    KEY idx_user_parent (user_id, parent_id),
    KEY idx_user (user_id),
    CONSTRAINT fk_folder_parent FOREIGN KEY (parent_id) REFERENCES folders(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 为 files 表添加 folder_id 字段
ALTER TABLE files ADD COLUMN folder_id BIGINT DEFAULT NULL AFTER user_id;
ALTER TABLE files ADD INDEX idx_folder (folder_id);
