# AeroImageHost 详细文档

## 系统架构

```
Browser → Nginx → C++ App (Drogon) → MySQL / Redis / MinIO
```

- **Nginx**: 反向代理，静态资源服务
- **C++ App**: 业务逻辑，端口 8082
- **MySQL**: 元数据存储（users、files、email_verifications）
- **Redis**: 会话缓存、登录限流、文件列表缓存
- **MinIO**: 文件对象存储（S3 兼容）

### 核心流程

**上传流程**: Client → 验证 → 生成 UUID → MinIO putObject → MySQL save → 返回 file_id

**缩略图**: Client → GET /api/i/{id}?size=N → 检查 MinIO 缓存 → 命中则返回，未命中则异步生成

**批量删除**: 并行执行 N 个 MinIO deleteObject → 单次 SQL 批量删除

## API 文档

所有 API 以 `/api` 为前缀，返回 JSON。

### 认证

| 方法 | 端点 | 说明 |
|------|------|------|
| POST | `/api/auth/register` | 注册 |
| POST | `/api/auth/login` | 登录（返回 token） |
| POST | `/api/auth/send-code` | 发送邮箱验证码 |
| POST | `/api/auth/email-register` | 邮箱注册 |

### 文件操作

| 方法 | 端点 | 说明 | 认证 |
|------|------|------|------|
| POST | `/api/upload` | 上传文件 | 是 |
| POST | `/api/upload/presign` | 获取预签名上传 URL | 是 |
| POST | `/api/upload/multipart/*` | 分片上传系列 | 是 |
| GET | `/api/files` | 文件列表（分页+搜索） | 是 |
| DELETE | `/api/file/{id}` | 删除文件 | 是 |
| POST | `/api/files/batch-delete` | 批量删除 | 是 |
| PUT | `/api/file/{id}/public` | 切换公开/私有 | 是 |
| GET | `/api/file/{id}/presign` | 获取预签名下载 URL | 是 |
| GET | `/api/share/{id}` | 分享信息 | 否 |
| GET | `/api/i/{id}` | 访问文件（支持 ?size=N 缩略图） | 公开文件否 |

### 系统

| 方法 | 端点 | 说明 |
|------|------|------|
| GET | `/api/health` | 健康检查 |
| GET | `/api/stats` | 系统统计 |
| GET | `/api/metrics` | 性能指标（需认证） |

### 认证头

```
Authorization: Bearer {your_token}
```

## 配置说明

### config.json

```json
{
  "http_port": 8082,
  "max_file_size": 104857600,
  "mysql": { "host": "localhost", "port": 3306, "user": "aero_user", "password": "", "db": "imagehost", "pool_size": 32 },
  "redis": { "host": "127.0.0.1", "port": 6379, "pool_size": 16, "password": "" },
  "minio": { "endpoint": "http://localhost:9000", "access_key": "minioadmin", "secret_key": "minioadmin123", "bucket": "images", "public_url": "http://localhost:8082/api/i/" },
  "security": { "cors_origin": "*", "max_login_attempts": 5, "login_window_seconds": 900 },
  "smtp": { "server": "smtp.qq.com", "port": 465, "username": "", "password": "", "from": "" },
  "log": { "file": "./logs/server.log", "flush_interval": 3 }
}
```

### 关键配置项

| 配置 | 默认值 | 说明 |
|------|--------|------|
| `http_port` | 8082 | 监听端口 |
| `max_file_size` | 104857600 | 单文件最大 100MB |
| `mysql.pool_size` | 32 | MySQL 连接池 |
| `redis.pool_size` | 16 | Redis 连接池 |
| `redis.password` | "" | Docker 下须与 .env 的 REDIS_PASSWORD 一致 |
| `minio.public_url` | http://localhost:8082/api/i/ | 文件公开访问 URL |
| `security.cors_origin` | * | CORS 允许域名（生产环境设具体源） |
| `security.max_login_attempts` | 5 | 登录失败次数限制 |
| `security.login_window_seconds` | 900 | 限流窗口 15 分钟 |

## 数据库表结构

### users
- id, account (unique), password_hash, email (unique, nullable), created_at

### files
- id, file_id (unique UUID), user_id, filename, size, mime_type, width, height, upload_time, is_public, view_count

### email_verifications
- id, email, verification_code, expires_at, created_at, used

## 部署指南

### Docker 一键部署

```bash
git clone https://github.com/ywx914705/AeroImageHost.git
cd AeroImageHost
bash deploy-docker.sh
```

### 本地编译部署

```bash
git clone https://github.com/ywx914705/AeroImageHost.git
cd AeroImageHost
bash deploy-local.sh
```

详细步骤见 [部署文档](DEPLOYMENT.md)。

### Nginx 反向代理

```nginx
server {
    listen 80;
    client_max_body_size 100M;
    location /api/ {
        proxy_pass http://127.0.0.1:8082/api/;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 300s;
    }
    location / {
        root /path/to/AeroImageHost/www;
        try_files $uri /index.html;
    }
}
```

## 监控

- 监控面板: `http://IP:8082/monitor/dashboard.html`（需登录）
- 健康检查: `GET /api/health`
- 性能指标: `GET /api/metrics`（需认证）
- 系统统计: `GET /api/stats`

## 性能

| 场景 | 表现 |
|------|------|
| 小文件上传 | < 500ms |
| 文件列表 | < 200ms |
| 图片重复访问 | < 10ms（304 缓存） |
| 内存占用 | 80-120MB |
