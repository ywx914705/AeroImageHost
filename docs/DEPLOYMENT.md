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

# 安装 Docker Compose 插件
sudo apt-get install -y docker-compose-plugin

# 验证
docker --version
docker compose version
```

### 配置镜像加速（国内服务器必做）

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

## Docker 部署

### 一键部署

```bash
git clone https://github.com/ywx914705/AeroImageHost.git
cd AeroImageHost
bash deploy-docker.sh
```

脚本自动完成：权限检查 → 镜像加速 → 配置文件 → 构建镜像 → 启动服务 → 等待就绪。

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

## 常见问题

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| Docker pull 超时 | 国内网络无法访问 Docker Hub | 配置镜像加速（见上方） |
| 权限不足 | 用户不在 docker 组 | `sudo usermod -aG docker $USER` 后重新登录 |
| Redis 连接失败 | 密码不一致 | 确保 `.env` 的 `REDIS_PASSWORD` 与 `config-docker.json` 的 `redis.password` 一致 |
| 页面 404 | Nginx 未正确配置 | 检查 `www/` 目录是否存在 |
| 上传失败 | 文件大小超限 | 检查 `max_file_size` 配置和 Nginx `client_max_body_size` |
