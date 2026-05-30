-- 07_hall.sql
-- 图床大厅：用户可以将公开图片发布到大厅供其他人浏览

CREATE TABLE IF NOT EXISTS hall_posts (
    id BIGINT NOT NULL AUTO_INCREMENT,
    file_id VARCHAR(64) NOT NULL,
    user_id INT NOT NULL,
    title VARCHAR(255) DEFAULT '',
    description TEXT,
    tags VARCHAR(500) DEFAULT '',
    likes INT DEFAULT 0,
    views INT DEFAULT 0,
    created_at BIGINT NOT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uk_file_id (file_id),
    KEY idx_created (created_at),
    KEY idx_user (user_id),
    KEY idx_likes (likes DESC)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS hall_likes (
    id BIGINT NOT NULL AUTO_INCREMENT,
    post_id BIGINT NOT NULL,
    user_id INT NOT NULL,
    created_at BIGINT NOT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uk_post_user (post_id, user_id),
    KEY idx_post (post_id),
    KEY idx_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
