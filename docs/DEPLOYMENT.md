# 部署指南

## 环境准备

### 服务器配置

| 项目 | 最低要求 | 推荐配置 |
|------|----------|----------|
| CPU | 2 核 | 4 核+ |
| 内存 | 2 GB | 4 GB+ |
| 磁盘 | 10 GB | 50 GB+（SSD） |
| 系统 | Ubuntu 20.04+ / CentOS 8+ | Ubuntu 22.04 LTS |
| 带宽 | 1 Mbps | 3 Mbps+ |

> 本项目已在 4 核 4GB / 3Mbps 云服务器上实测稳定运行。

### 安装 Docker

```bash
# 安装 Docker
curl -fsSL https://get.docker.com | sh
sudo systemctl enable --now docker

# 安装 Docker Compose（V2 插件版，推荐）
sudo apt-get install -y docker-compose-plugin

# 验证
docker --version
docker compose version
```

> 如果 `docker compose` 不可用，系统可能安装的是 V1 版本（`docker-compose`），部署脚本会自动检测并适配。

### 配置镜像加速（国内服务器必做）

Docker Hub 在国内经常超时，不配置加速会导致拉取镜像失败。

```bash
sudo mkdir -p /etc/docker
sudo tee /etc/docker/daemon.json << 'EOF'
{
  "registry-mirrors": [
    "https://docker.1ms.run",
    "https://docker.xuanyuan.me"
  ]
}
EOF
sudo systemctl daemon-reload && sudo systemctl restart docker
```

> **注意**：如果 `/etc/docker/daemon.json` 已有其他配置，需要合并而非覆盖。检查现有配置：`cat /etc/docker/daemon.json`

## Docker 部署

### 一键部署（推荐）

```bash
git clone https://github.com/ywx914705/AeroImageHost.git
cd AeroImageHost
bash deploy-docker.sh
```

脚本自动完成：Docker 权限检查 → 镜像加速配置 → 配置文件生成 → 构建镜像 → 启动服务 → 等待就绪 → 显示访问地址。

### 手动部署

```bash
# 1. 克隆项目
git clone https://github.com/ywx914705/AeroImageHost.git
cd AeroImageHost

# 2. 配置环境变量
cp .env.example .env

# 3. 配置应用
cp config/config-docker.example.json config/config-docker.json

# 4. 启动
docker compose up -d

# 5. 等待就绪
docker compose ps
```

### 密码配置

`.env` 和 `config/config-docker.json` 中的密码已预设为默认值，可直接使用：

| 配置项 | 默认值 |
|--------|--------|
| MySQL root 密码 | root123456 |
| MySQL 应用密码 | aero123456 |
| MinIO 密码 | minio123456 |
| Redis 密码 | redis123456 |

> 生产环境请务必修改所有密码。

## 本地编译部署

### 一键部署

```bash
git clone https://github.com/ywx914705/AeroImageHost.git
cd AeroImageHost
bash deploy-local.sh
```

### 手动编译

```bash
# 安装依赖
sudo apt-get install -y build-essential cmake pkg-config git \
    libssl-dev libcurl4-openssl-dev libmysqlclient-dev libvips-dev \
    libhiredis-dev libcurlpp-dev libpugixml-dev libinih-dev

# 编译
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# 运行
./AeroImageHost ../config.json
```

## 访问服务

| 服务 | 地址 | 说明 |
|------|------|------|
| Web 界面 | `http://服务器IP:8082` | 主界面，文件管理 |
| 监控面板 | `http://服务器IP:8082/monitor/dashboard.html` | 实时性能监控 |
| MinIO 控制台 | `http://服务器IP:9090` | 对象存储管理 |

## 运维命令

```bash
docker compose ps              # 查看服务状态
docker compose logs -f app     # 查看应用日志
docker compose restart app     # 重启应用
docker compose down            # 停止所有服务
docker compose up -d           # 重新启动
docker compose build           # 重新构建镜像
```

## Nginx 反向代理（可选）

```nginx
server {
    listen 80;
    server_name your-domain.com;
    client_max_body_size 100M;

    location /api/ {
        proxy_pass http://127.0.0.1:8082/api/;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_read_timeout 300s;
        proxy_send_timeout 300s;
    }

    location / {
        root /path/to/AeroImageHost/www;
        try_files $uri /index.html;
    }
}
```

## 常见问题与排查

### Docker 相关

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| `docker: 'compose' is not a docker command` | Docker Compose 未安装为 V2 插件 | `sudo apt-get install -y docker-compose-plugin`，或使用 V1：`docker-compose` |
| `permission denied while trying to connect to Docker daemon` | 用户不在 docker 组 | `sudo usermod -aG docker $USER` 后执行 `newgrp docker` 或重新登录 |
| `Get "https://registry-1.docker.io/v2/": context deadline exceeded` | 国内无法访问 Docker Hub | 配置镜像加速（见上方），或检查 `/etc/docker/daemon.json` 是否已配置 |
| `the following directives are specified both as a flag and in the configuration file` | `daemon.json` 和 systemd drop-in 都配置了 `registry-mirrors` | 只保留一处：优先使用 systemd drop-in（`/etc/systemd/system/docker.service.d/`） |
| `Error response from daemon: No such container` | 容器已被删除或未创建 | 重新运行 `docker compose up -d --build` |

### 构建相关

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| `fatal: unable to access 'https://github.com/...'` | Docker 容器内无法访问 GitHub | Dockerfile 已配置 ghproxy.com 镜像回退，确保网络通畅 |
| `Cannot find source file: tests/forwarded_for_parse_test.cc` | tests 目录已删除但 CMake 仍引用 | Dockerfile 使用 `-DBUILD_TESTING=OFF`，如仍报错检查 CMakeLists.txt |
| `apt-get update` 超时 | 容器内 apt 源不可用 | Dockerfile 已配置阿里云镜像，检查 `/etc/apt/sources.list` |

### 服务相关

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| `Redis connection refused` | Redis 密码不一致 | 确保 `.env` 的 `REDIS_PASSWORD` 与 `config-docker.json` 的 `redis.password` 完全一致 |
| `dependency failed to start: container xxx is unhealthy` | 某个依赖服务启动失败 | `docker compose logs xxx` 查看具体错误 |
| 页面 404 或白屏 | Nginx 未正确代理 | 检查 `www/` 目录存在且包含 `index.html` |
| 上传失败 | 文件大小超限 | 检查 `max_file_size` 配置和 Nginx `client_max_body_size` |
| 403 Forbidden | Nginx 权限或配置错误 | 检查 `nginx.conf` 和文件权限 |
| 从外网无法访问 | 端口未开放 | 检查云服务器安全组/防火墙是否放行 8082 和 9090 端口 |

### 网络相关

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| Docker pull 极慢 | 国内网络访问 Docker Hub 慢 | 配置镜像加速，或使用 `docker-compose` V1（部分镜像源对 V1 支持更好） |
| MinIO 镜像拉取超时 | 特定版本 tag 不在镜像源中 | docker-compose.yml 已改用 `minio/minio:latest` |
| GitHub 访问超时 | 国内访问 GitHub 不稳定 | Dockerfile 已配置 ghproxy.com 镜像回退 |

### 端口说明

| 端口 | 服务 | 说明 |
|------|------|------|
| 8082 | Nginx → App | Web 界面和 API |
| 9090 | MinIO Console | MinIO 管理控制台 |
| 3306 | MySQL | 数据库（仅容器内） |
| 6379 | Redis | 缓存（仅容器内） |
| 9000 | MinIO API | 对象存储 API（仅容器内） |
