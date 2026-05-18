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

# 检测 Docker 权限和 Compose 命令
DOCKER_CMD="docker"
COMPOSE_CMD=""

if docker ps &> /dev/null; then
    echo "[OK] Docker 权限正常"
    # V2
    if docker compose version &> /dev/null; then
        COMPOSE_CMD="docker compose"
    elif command -v docker-compose &> /dev/null; then
        COMPOSE_CMD="docker-compose"
    fi
elif sudo -n docker ps &> /dev/null 2>&1; then
    DOCKER_CMD="sudo docker"
    echo "[OK] 使用 sudo 执行 docker"
    # V2 with sudo
    if sudo docker compose version &> /dev/null 2>&1; then
        COMPOSE_CMD="sudo docker compose"
    elif command -v docker-compose &> /dev/null; then
        COMPOSE_CMD="sudo docker-compose"
    fi
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
echo "[2/4] 构建并启动服务..."
echo "  首次构建约 10-20 分钟，请耐心等待"
$COMPOSE_CMD up -d --build

echo ""
echo "[3/4] 配置前端文件..."
# 将 www 目录复制到 nginx 容器
$DOCKER_CMD cp www/index.html aeroimagehost-nginx:/usr/share/nginx/html/index.html 2>/dev/null || true
$DOCKER_CMD cp www/assets aeroimagehost-nginx:/usr/share/nginx/html/assets 2>/dev/null || true
$DOCKER_CMD cp www/monitor aeroimagehost-nginx:/usr/share/nginx/html/monitor 2>/dev/null || true
# 重启 nginx 使配置生效
$DOCKER_CMD restart aeroimagehost-nginx 2>/dev/null || true
echo "  前端文件已配置，nginx 已重启"

echo ""
echo "[4/4] 等待服务就绪..."
for i in {1..120}; do
    if $COMPOSE_CMD ps 2>/dev/null | grep -q "healthy"; then
        echo "  服务已就绪！"
        break
    fi
    sleep 5
    if [ $((i % 12)) -eq 0 ]; then
        echo "  等待中... ($((i/2))分钟)"
    fi
done

# 获取公网 IP
IP=$(curl -s --connect-timeout 5 https://api.ipify.org 2>/dev/null)
if [ -z "$IP" ]; then
    IP=$(curl -s --connect-timeout 5 https://ifconfig.me 2>/dev/null)
fi
if [ -z "$IP" ]; then
    IP=$(curl -s --connect-timeout 5 http://ip.sb 2>/dev/null)
fi
if [ -z "$IP" ]; then
    # 尝试通过 MinIO 获取公网 IP
    IP=$(curl -s --connect-timeout 5 http://localhost:9000/minio/health/live 2>/dev/null | grep -oP '"ip":"[^"]*"' | cut -d'"' -f4)
fi
if [ -z "$IP" ]; then
    IP=$(hostname -I | awk '{print $1}')
    echo "[提示] 无法获取公网 IP，使用内网 IP: $IP"
fi
URL="http://${IP}:8082"

echo ""
echo "=========================================="
echo "  部署完成！"
echo "=========================================="
echo ""
echo "  请使用公网 IP 访问以下地址："
echo ""
echo "  图床主界面:  ${URL}"
echo "  监控面板:    ${URL}/monitor/dashboard.html"
echo "  MinIO 控制台: http://${IP}:9090"
echo "    (用户名: minioadmin, 密码: minio123456)"
echo ""
echo "  ──────────────────────────────────────"
echo "  首次使用说明："
echo "  ──────────────────────────────────────"
echo ""
echo "  1. 打开 ${URL} 进行登录或注册"
echo "  2. 注册时需要填写邮箱（仅支持 QQ 邮箱）"
echo "     如需启用邮箱验证，请编辑 config/config-docker.json"
echo "     修改 smtp 部分填写你的 QQ 邮箱信息："
echo '     "smtp": {'
echo '       "server": "smtp.qq.com",'
echo '       "port": 465,'
echo '       "username": "你的QQ邮箱@qq.com",'
echo '       "password": "你的SMTP授权码",'
echo '       "from": "你的QQ邮箱@qq.com"'
echo '     }'
echo "  3. 修改后重启应用: $DOCKER_CMD restart aeroimagehost-app"
echo ""
echo "  ──────────────────────────────────────"
echo "  常用命令："
echo "  ──────────────────────────────────────"
echo ""
echo "  $DOCKER_CMD compose ps              # 查看服务状态"
echo "  $DOCKER_CMD compose logs -f app     # 查看应用日志"
echo "  $DOCKER_CMD compose restart app     # 重启应用"
echo "  $DOCKER_CMD compose down            # 停止服务"
echo ""

# 尝试自动打开浏览器
if command -v xdg-open &> /dev/null; then
    xdg-open "$URL" 2>/dev/null &
elif command -v open &> /dev/null; then
    open "$URL" 2>/dev/null &
fi

echo "  常用命令："
echo "  $COMPOSE_CMD ps              # 查看状态"
echo "  $COMPOSE_CMD logs -f app     # 查看日志"
echo "  $COMPOSE_CMD restart app     # 重启应用"
echo "  $COMPOSE_CMD down            # 停止服务"
