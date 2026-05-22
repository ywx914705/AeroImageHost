-- 添加水印相关字段
ALTER TABLE files ADD COLUMN watermark_text VARCHAR(255) DEFAULT NULL;
ALTER TABLE files ADD COLUMN watermark_position VARCHAR(20) DEFAULT 'bottom-right';
ALTER TABLE files ADD COLUMN watermark_opacity INT DEFAULT 50;
