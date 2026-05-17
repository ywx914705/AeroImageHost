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
    echo "请先安装: curl -fsSL https://get.docker.com | sh"
    exit 1
fi

# 检查 Docker Compose 是否可用
if ! docker compose version &> /dev/null; then
    echo "[错误] Docker Compose 未安装"
    echo "请先安装: sudo apt-get install -y docker-compose-plugin"
    exit 1
fi

# 检测 Docker 权限
DOCKER_CMD="docker"
if docker ps &> /dev/null; then
    echo "[OK] Docker 权限正常"
elif sudo -n docker ps &> /dev/null 2>&1; then
    DOCKER_CMD="sudo docker"
    echo "[OK] 使用 sudo 执行 docker"
else
    echo "[提示] 当前用户无 Docker 权限，尝试自动添加..."
    if sudo usermod -aG docker "$USER" 2>/dev/null; then
        echo "[OK] 已将用户 $USER 添加到 docker 组"
        echo "[提示] 请执行: newgrp docker  然后重新运行此脚本"
        exit 0
    else
        echo "[错误] 请手动执行: sudo usermod -aG docker \$USER && newgrp docker"
        exit 1
    fi
fi

# 配置国内镜像加速
if ! $DOCKER_CMD info 2>/dev/null | grep -q "Registry Mirrors"; then
    echo ""
    echo "[提示] 配置 Docker 镜像加速..."
    sudo mkdir -p /etc/docker
    sudo tee /etc/docker/daemon.json > /dev/null << 'EOF'
{
  "registry-mirrors": [
    "https://docker.1ms.run",
    "https://docker.xuanyuan.me"
  ]
}
EOF
    sudo systemctl daemon-reload
    sudo systemctl restart docker
    echo "[OK] 镜像加速已配置"
    if ! docker ps &> /dev/null; then
        DOCKER_CMD="sudo docker"
    fi
fi

echo ""
echo "[1/3] 配置文件..."
if [ ! -f .env ]; then
    cp .env.example .env
    echo "  已创建 .env（使用默认密码）"
else
    echo "  .env 已存在"
fi

if [ ! -f config/config-docker.json ]; then
    cp config/config-docker.example.json config/config-docker.json
    echo "  已创建 config/config-docker.json"
else
    echo "  config-docker.json 已存在"
fi

echo ""
echo "[2/3] 构建并启动服务..."
echo "  首次构建约 10-20 分钟，请耐心等待"
$DOCKER_CMD compose up -d --build

echo ""
echo "[3/3] 等待服务就绪..."
for i in {1..120}; do
    if $DOCKER_CMD compose ps 2>/dev/null | grep -q "healthy"; then
        echo "  服务已就绪！"
        break
    fi
    sleep 5
    if [ $((i % 12)) -eq 0 ]; then
        echo "  等待中... ($((i/2))分钟)"
    fi
done

IP=$(hostname -I | awk '{print $1}')
URL="http://${IP}:8082"

echo ""
echo "=========================================="
echo "  部署完成！"
echo "=========================================="
echo ""
echo "  Web 界面: ${URL}"
echo "  MinIO 控制台: http://${IP}:9090"
echo ""

# 尝试自动打开浏览器
if command -v xdg-open &> /dev/null; then
    xdg-open "$URL" 2>/dev/null &
elif command -v open &> /dev/null; then
    open "$URL" 2>/dev/null &
fi

echo "  常用命令："
echo "  $DOCKER_CMD compose ps              # 查看状态"
echo "  $DOCKER_CMD compose logs -f app     # 查看日志"
echo "  $DOCKER_CMD compose restart app     # 重启应用"
echo "  $DOCKER_CMD compose down            # 停止服务"
