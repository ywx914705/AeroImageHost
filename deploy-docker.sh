#!/bin/bash
# AeroImageHost Docker 一键部署脚本
# 使用方法: bash deploy-docker.sh
set -e

echo "=========================================="
echo "  AeroImageHost Docker 部署脚本"
echo "=========================================="
echo ""

# 检查 Docker 是否安装
if ! command -v docker &> /dev/null; then
    echo "[错误] Docker 未安装"
    echo "请先安装 Docker: curl -fsSL https://get.docker.com | sh"
    exit 1
fi

# 检查 Docker Compose 是否可用
if ! docker compose version &> /dev/null; then
    echo "[错误] Docker Compose 未安装"
    echo "请先安装: sudo apt-get install -y docker-compose-plugin"
    exit 1
fi

echo "[1/4] 配置环境变量..."
if [ ! -f .env ]; then
    cp .env.example .env
    echo "  已创建 .env 文件，请修改其中的密码"
    echo "  修改方法: nano .env"
    echo ""
    echo "  需要修改的项："
    echo "  - MYSQL_ROOT_PASSWORD (MySQL root 密码)"
    echo "  - MYSQL_PASSWORD (应用数据库密码)"
    echo "  - MINIO_ROOT_PASSWORD (MinIO 密码)"
    echo "  - REDIS_PASSWORD (Redis 密码)"
    echo ""
    read -p "按回车继续（确保已修改 .env 中的密码）..."
else
    echo "  .env 文件已存在，跳过"
fi

echo ""
echo "[2/4] 配置应用..."
if [ ! -f config/config-docker.json ]; then
    cp config/config-docker.example.json config/config-docker.json
    echo "  已创建 config/config-docker.json"
    echo ""
    echo "  重要：请确保 config/config-docker.json 中的 redis.password"
    echo "  与 .env 中的 REDIS_PASSWORD 完全一致！"
    echo ""
    read -p "按回车继续..."
else
    echo "  config/config-docker.json 已存在，跳过"
fi

echo ""
echo "[3/4] 启动服务..."
echo "  首次启动需要构建镜像，约 5-15 分钟"
docker compose up -d --build

echo ""
echo "[4/4] 等待服务就绪..."
echo "  正在等待所有服务 healthy..."
for i in {1..60}; do
    if docker compose ps 2>/dev/null | grep -q "healthy"; then
        echo "  服务已就绪！"
        break
    fi
    sleep 5
    echo "  等待中... ($i/60)"
done

echo ""
echo "=========================================="
echo "  部署完成！"
echo "=========================================="
echo ""
echo "  Web 界面: http://$(hostname -I | awk '{print $1}'):8082"
echo "  MinIO 控制台: http://$(hostname -I | awk '{print $1}'):9090"
echo ""
echo "  常用命令："
echo "  docker compose ps              # 查看状态"
echo "  docker compose logs -f app     # 查看日志"
echo "  docker compose restart app     # 重启应用"
echo "  docker compose down            # 停止服务"
