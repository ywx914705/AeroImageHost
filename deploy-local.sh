#!/bin/bash
# AeroImageHost 本地编译部署脚本
# 使用方法: bash deploy-local.sh
set -e

echo "=========================================="
echo "  AeroImageHost 本地编译部署脚本"
echo "=========================================="
echo ""

# 检查操作系统
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo "[错误] 此脚本仅支持 Linux 系统"
    exit 1
fi

# 检测包管理器
PKG_MGR=""
if command -v apt-get &> /dev/null; then
    PKG_MGR="apt-get"
elif command -v yum &> /dev/null; then
    PKG_MGR="yum"
elif command -v dnf &> /dev/null; then
    PKG_MGR="dnf"
else
    echo "[错误] 未检测到包管理器（apt-get/yum/dnf）"
    exit 1
fi
echo "[OK] 包管理器: $PKG_MGR"

echo "[1/6] 检查并安装依赖..."
if [ "$PKG_MGR" = "apt-get" ]; then
    sudo apt-get update -qq
    sudo apt-get install -y -qq build-essential cmake pkg-config git \
        libssl-dev libcurl4-openssl-dev libmysqlclient-dev libvips-dev \
        libhiredis-dev libcurlpp-dev libpugixml-dev libinih-dev \
        libjsoncpp-dev uuid-dev libbrotli-dev 2>/dev/null
elif [ "$PKG_MGR" = "yum" ] || [ "$PKG_MGR" = "dnf" ]; then
    sudo $PKG_MGR install -y gcc gcc-c++ cmake pkg-config git \
        openssl-devel libcurl-devel mysql-devel vips-devel \
        hiredis-devel curlpp-devel pugixml-devel inih-devel \
        jsoncpp-devel uuid-devel brotli-devel 2>/dev/null
fi

echo "[2/6] 检查 Drogon 框架..."
if ! pkg-config --exists drogon 2>/dev/null && ! ldconfig -p 2>/dev/null | grep -q libdrogon; then
    echo "  Drogon 未安装，正在编译安装..."
    cd /tmp
    git clone --depth 1 --branch v1.9.4 https://github.com/drogonframework/drogon.git
    cd drogon
    git submodule update --init --recursive
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF ..
    make -j$(nproc)
    sudo make install
    sudo ldconfig
    cd /
    rm -rf /tmp/drogon
    echo "  Drogon 安装完成"
else
    echo "  Drogon 已安装"
fi

echo "[3/6] 检查 miniocpp..."
if ! ldconfig -p 2>/dev/null | grep -q libminiocpp; then
    echo "  miniocpp 未安装，正在编译安装..."
    cd /tmp
    curl -L -o minio-cpp.tar.gz https://github.com/minio/minio-cpp/archive/refs/heads/master.tar.gz --retry 3 --max-time 120
    tar -xzf minio-cpp.tar.gz
    mv minio-cpp-* minio-cpp
    cd minio-cpp
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release .. 2>/dev/null || true
    make -j$(nproc)
    sudo make install
    sudo ldconfig
    cd /
    rm -rf /tmp/minio-cpp /tmp/minio-cpp.tar.gz
    echo "  miniocpp 安装完成"
else
    echo "  miniocpp 已安装"
fi

echo "[4/6] 初始化数据库..."
echo "  请确保 MySQL 已运行，然后执行："
echo "  mysql -u root -p < schema/01_init.sql"
echo "  mysql -u root -p < schema/02_email_verification.sql"
echo ""
read -p "按回车继续（确保数据库已初始化）..."

echo "[5/6] 编译项目..."
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
cd ..

echo "[6/6] 创建配置文件..."
if [ ! -f config.json ]; then
    cat > config.json << 'EOF'
{
  "http_port": 8082,
  "max_file_size": 104857600,
  "stats_cache_ttl_seconds": 60,
  "mysql": {
    "host": "localhost",
    "port": 3306,
    "user": "aero_user",
    "password": "your_password",
    "db": "imagehost",
    "pool_size": 32
  },
  "redis": {
    "host": "127.0.0.1",
    "port": 6379,
    "pool_size": 16,
    "password": ""
  },
  "minio": {
    "endpoint": "http://localhost:9000",
    "access_key": "minioadmin",
    "secret_key": "minioadmin123",
    "bucket": "images",
    "public_url": "http://localhost:8082/api/i/"
  },
  "security": {
    "cors_origin": "http://localhost:8082",
    "max_login_attempts": 5,
    "login_window_seconds": 900
  },
  "log": {
    "file": "./logs/server.log",
    "flush_interval": 3
  }
}
EOF
    echo "  已创建 config.json，请修改其中的数据库密码"
else
    echo "  config.json 已存在，跳过"
fi

echo ""
echo "=========================================="
echo "  编译完成！"
echo "=========================================="
echo ""
echo "  启动命令: ./build/AeroImageHost config.json"
echo "  访问地址: http://localhost:8082"
