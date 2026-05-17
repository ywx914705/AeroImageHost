# 部署指南

## 快速部署（推荐）

### Docker 一键部署

```bash
git clone https://github.com/ywx914705/AeroImageHost.git
cd AeroImageHost
bash deploy-docker.sh
```

脚本自动完成：安装检查 → 配置环境变量 → 配置应用 → 构建镜像 → 启动服务 → 等待就绪。

### 本地编译部署

```bash
git clone https://github.com/ywx914705/AeroImageHost.git
cd AeroImageHost
bash deploy-local.sh
```

脚本自动完成：安装依赖 → 编译 Drogon/miniocpp → 编译项目 → 创建配置。

## 访问服务

| 服务 | 地址 |
|------|------|
| Web 界面 | http://服务器IP:8082 |
| 监控面板 | http://服务器IP:8082/monitor/dashboard.html |
| MinIO 控制台 | http://服务器IP:9090 |

## 常用运维命令

```bash
docker compose ps              # 查看状态
docker compose logs -f app     # 应用日志
docker compose restart app     # 重启应用
docker compose down            # 停止服务
```

## Nginx 反向代理（可选）

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
