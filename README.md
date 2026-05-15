# 🚀 AeroImageHost - 高性能现代化图床系统

<div align="center">
  <img src="www/assets/logo.png" alt="AeroImageHost Logo" width="140" style="border-radius: 20px; box-shadow: 0 8px 24px rgba(0,0,0,0.15);">
  <br><br>
  <p><strong>极速 · 安全 · 可靠的现代化图床系统</strong></p>
  <p>C++17 + Drogon · MinIO 分布式存储 · Redis 会话管理 · Docker 一键部署</p>
  <br>
</div>

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg?logo=cplusplus)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.10+-064F8C.svg?logo=cmake)](https://cmake.org/)
[![MySQL 8.0](https://img.shields.io/badge/MySQL-8.0-4479A1.svg?logo=mysql)](https://www.mysql.com/)
[![MinIO S3](https://img.shields.io/badge/MinIO-S3-FE2C25.svg?logo=minio)](https://min.io/)
[![Redis](https://img.shields.io/badge/Redis-7.x-DC382D.svg?logo=redis)](https://redis.io/)
[![Docker](https://img.shields.io/badge/Docker-Ready-2496ED.svg?logo=docker)](https://www.docker.com/)
[![libvips](https://img.shields.io/badge/libvips-8.0+-f5a3b8.svg?logo=vips)](https://github.com/libvips/libvips)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**AeroImageHost** 是一个用 **C++17** 编写的高性能现代化图床系统，采用组件化单体架构设计，集成了 MySQL、MinIO、Redis 等技术栈。系统提供完整的用户认证、文件管理、图片处理和分布式存储功能，支持 Docker 一键部署，适用于个人、团队和企业级文件托管需求。

### 🖥️ 前端（用户界面）

- **形态**: 单文件 `www/index.html`，**Vue 3 / Element Plus / Axios / dayjs** 从 `www/assets/` 本地加载（与 `nginx.conf` 的 `/assets/` 一致，无公网 CDN 依赖）。
- **品牌资源**: `www/assets/logo.png`、`www/assets/favicon.svg` 为仓库内置图标；部署后通过 `/assets/` 访问（与 `nginx.conf` 中静态资源路径一致）。
- **监控页**: `www/monitor/dashboard.html` 为独立仪表盘，路径为 `/monitor/dashboard.html`；请求 `/api/metrics` 时会读取与主站相同的 `localStorage` token。
- **接口约定**: 前端 `axios` 的 `baseURL` 为 `/api`；缩略图列表使用 `GET /api/i/{file_id}?size=200`（与后端 `main.cc` 中 `size` 查询参数一致）。

## ✨ 核心亮点

| 特性             | 描述                 | 优势                             |
| -------------- | ------------------ | ------------------------------ |
| **⚡ 极速性能**     | C++17 + 连接池 + 异步处理 | 轻量级设计，内存占用 80\~120MB           |
| **🔒 智能上传**    | 小文件代理 + 大文件分片直传    | 断点续传 + 真实进度 + 页面刷新恢复           |
| **🖼️ 智能图片处理** | libvips 驱动缩略图      | 内存占用少，处理速度比 ImageMagick 快 10 倍 |
| **🎯 精准搜索**    | 文件名模糊搜索 + 分页       | 支持复杂查询条件                       |
| **🌐 分布式存储**   | MinIO 对象存储         | S3 兼容，支持水平扩展和高可用               |
| **🔄 异步架构**    | 任务队列 + 非阻塞 I/O     | 高并发下依然保持低延迟                    |
| **🐳 容器化部署**   | Docker Compose 编排  | 一键部署，开箱即用，生产就绪                 |

## 📦 功能特性一览

### 🔐 用户认证系统

- **双注册方式**: 普通账号注册 + 邮箱验证码注册
- **Token 鉴权**: Redis 存储会话 Token，支持 24 小时自动过期
- **安全哈希**: PBKDF2-HMAC-SHA256（10 万次迭代 + 随机盐），兼容旧版 SHA-256 自动升级
- **权限控制**: 文件级私有/公开权限管理

### 📤 文件上传与管理

- **双上传模式**:
  - **直接上传**: 适合小文件 (< 5MB)，服务端中转上传
  - **分片上传**: 大文件分片直传 MinIO，支持断点续传和真实进度追踪
- **文件类型检测**: 智能 MIME 类型识别，防止恶意文件
- **文件操作**: 支持文件列表、搜索、删除、公开/私有切换
- **批量管理**: 支持多选、全选、批量删除文件
- **多格式预览**: 支持 PDF 文档、视频（MP4/WebM/MOV 等）、音频（MP3/WAV/FLAC 等）在线预览
- **智能缩略图**: 按需生成，支持自定义尺寸 (如 `?size=200`)

### 🏗️ 系统架构

- **组件化设计**: 模块解耦，易于维护和扩展
- **连接池管理**: MySQL / Redis 连接池大小由 `config.json` 中 `mysql.pool_size`、`redis.pool_size` 配置（非固定 32/16）
- **异步日志**: 高性能异步日志系统，不影响主业务
- **健康检查**: 完善的 Docker 健康检查机制

### 🛠️ 开发者友好

- **完整 API 文档**: RESTful 接口设计，易于集成
- **Docker 化**: 生产环境一键部署
- **详细日志**: 多级别日志输出，便于调试
- **配置灵活**: JSON 配置文件，按需修改

## 🏗️ 系统架构详解

### 整体架构图

```
                          ┌─────────────────────┐
                          │   Browser / Client  │
                          │     Vue 3 SPA       │
                          └─────────┬───────────┘
                                    │ HTTP REST API
                                    ▼
                ┌───────────────────────────────────┐
                │  Nginx (Docker 反向代理)            │
                │  /api/ → App  / → Frontend         │
                └───────────────┬───────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────┐
│                    C++ App (port 8082)                       │
│                                                              │
│  ┌──────────┐  ┌──────────┐  ┌────────────────────────────┐ │
│  │ Drogon   │─→│  Auth    │─→│       Handlers              │ │
│  │Controllers│  │Filter验证│  │  业务逻辑 → HandlerResult   │ │
│  └──────────┘  └──────────┘  └──┬─────┬─────┬─────┬───────┘ │
│                                 │     │     │     │         │
│                   ┌─────────────┘     │     │     │         │
│                   ▼                   ▼     ▼     ▼         │
│          ┌──────────────┐  ┌───────┐ ┌────┐ ┌──────────┐   │
│          │ FileMetaDAO  │  │Users  │ │E-  │ │ MinIO    │   │
│          │ files 表 SQL │  │DAO    │ │mail│ │ Client   │   │
│          └──────┬───────┘  │users表│ │DAO │ └────┬─────┘   │
│                 │          └──┬────┘ └──┬─┘      │         │
│                 │             │         │        │         │
│                 ▼             ▼         ▼        ▼         │
│  ┌────────────────────────────────────────────────────┐    │
│  │          ConnectionPool (MySQL 连接池 32)           │    │
│  └────────────────────────┬───────────────────────────┘    │
│                           │                                │
│  ┌──────────────┐  ┌──────┴─────┐  ┌──────────────────┐   │
│  │  AeroQueue   │  │ RedisClient│  │  ImageProcessor   │   │
│  │  4 工作线程  │  │ 连接池(可配)│  │  libvips 缩略图   │   │
│  │  异步任务    │  └──────┬─────┘  └──────────────────┘   │
│  └──────┬───────┘         │                               │
└─────────┼─────────────────┼───────────────────────────────┘
          │                 │
          ▼                 ▼
┌──────────────────────────────────────────────────────────────┐
│                        外部服务                               │
│                                                              │
│  ┌──────────────┐  ┌──────────┐  ┌──────────────────────┐   │
│  │  MySQL 8.0   │  │ Redis 7  │  │       MinIO          │   │
│  │              │  │          │  │                      │   │
│  │  users       │  │  Token   │  │  objects/   文件存储  │   │
│  │  files       │  │  缓存    │  │  thumbs/   缩略图缓存│   │
│  │  email_verif │  │  限流    │  │  chunks/   分片暂存   │   │
│  └──────────────┘  └──────────┘  └──────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

#### 数据流说明

```
上传(小文件直传): Client → Handlers::handleUpload
    → MinIO putObject（对象键为 file_id）
    → FileMetaDAO::save（失败则 deleteObject 回滚对象）

缩略图: Client → GET /api/i/{file_id}?size=N → main.cc（命中 MinIO thumbs/{file_id}_N 则 302 预签名；否则排队异步生成缩略图并回写 MinIO）
                             └→ AeroQueue(异步) → MinIO thumbs/ 缓存

批量删除: Client → Handlers → FileMetaDAO(验证归属权) ──std::async 并行──→ MinIOClient × N → MinIO
                           └→ FileMetaDAO(批量删除) → MySQL

邮件: Client → Handlers → AeroQueue(fire-and-forget) → SMTP
```

### 🔄 核心流程

#### 1. 用户认证流程

```
用户注册 → PBKDF2-HMAC-SHA256（随机盐 + 10 万次迭代） → 存储到 MySQL
    ↓
用户登录 → 验证密码 → 生成 32 位随机 Token → 存储到 Redis (24h)
    ↓
API 请求 → 携带 Bearer Token → Redis 验证 → 授权访问
```

#### 2. 文件上传流程（直接模式）

```
客户端 → POST /api/upload → 携带文件数据
    ↓
服务端 → 验证文件类型和大小 → 生成 UUID 作为 file_id
    ↓
MinIO putObject（先写对象，失败则返回 500）
    ↓
FileMetaDAO::save（MySQL 元数据；失败则 deleteObject 删除已上传对象并返回错误）
    ↓
返回 → file_id、presign_url、download_url 等 JSON 字段
```

#### 3. 缩略图请求流程（带缓存）

```
客户端 → GET /api/i/{file_id}?size=200
    ↓
检查 MinIO 对象键 thumbs/{file_id}_200 是否存在
    ├─ 命中 → 302 重定向至缩略图预签名 URL
    └─ 未命中 → 投递 AeroQueue 异步生成缩略图；当前请求 302 至原图预签名 URL（下次请求通常命中缓存）
```

#### 4. 文件上传流程（分片上传模式）

```
客户端 → POST /api/upload/multipart/init → 获取 upload_id + 分片信息
    ↓
客户端 → POST /api/upload/multipart/chunk?upload_id=X&part_number=N → 逐片经服务器代理上传到 MinIO（XHR 真实进度 + localStorage 断点记录）
    ↓
客户端 → POST /api/upload/multipart/complete → 合并分片
    ↓
服务端 → 构造分片 key 列表 → MinIO ComposeObject 服务端合并（零下载） → 存储元数据 → 返回下载链接
```

#### 5. 批量删除流程

```
客户端 → POST /api/files/batch-delete → { "file_ids": [...] }
    ↓
FileMetaDAO::getValidFileIds → 单次 SQL 查询验证归属权
    ↓
std::async 并行 → N 个 MinIO deleteObject 同时执行
    ↓
FileMetaDAO::deleteFilesBatch → 单次 SQL 批量删除 MySQL 记录
    ↓
返回 → { deleted_count, failed_count }
```

### ⚡ 性能优化设计

#### 连接池管理

```cpp
// MySQL 连接池：大小由 mysql.pool_size 配置，定时连接验证
class ConnectionPool {
    // 每 30 秒验证一次（ensureValidConnection + mysql_ping）
    // 归还免验证，延迟到 getConnection 按需执行
    // 连接超时保护（5 秒）
    // 连接失效自动重建
};

// Redis 连接池：大小由配置 redis.pool_size 决定，命令级并发
class RedisClient {
    // 连接复用
    // 自动错误恢复
    // 资源自动释放
};
```

#### 异步处理架构

```cpp
// AeroQueue：基于无锁并发队列的异步任务处理器（4 工作线程）
class AeroQueue {
    // 用于：缩略图缓存异步回写 MinIO、邮件异步发送
    // 基于 concurrentqueue.hpp 无锁 FIFO 队列
    // 优雅关闭机制（stop + join）
};

// std::async 并行：用于批量删除等多对象 MinIO I/O
// 小文件直传 handleUpload：先 MinIO putObject，再 MySQL save；save 失败时 deleteObject 回滚对象
// 批量删除：N 个 MinIO deleteObject 并行执行
```

## 📊 性能基准

### 测试环境

- **CPU**: 4 核 Intel Xeon
- **内存**: 4GB RAM
- **存储**: SSD 云盘
- **网络**: 300Mbps 带宽
- **OS**: Ubuntu 22.04 LTS
- **部署方式**: Docker Compose 单机部署

> 以下数据为架构分析预估值，建议使用 wrk 或 ab 工具进行实际压测验证。

### 预期性能表现

| 场景         | 预期表现        | 说明                          |
| ---------- | ----------- | --------------------------- |
| **小文件上传**  | < 500ms     | 1MB 以内文件，包含元数据存储            |
| **大文件上传**  | 网络带宽限制      | 100MB 文件约 3-4 秒（300Mbps 带宽） |
| **文件列表查询** | < 200ms     | 1000 文件以内，带分页               |
| **文件搜索**   | < 300ms     | 基于文件名的模糊匹配                  |
| **图片重复访问** | < 10ms      | 浏览器 304 缓存命中                |
| **并发处理**   | 200-300 QPS | 4 核 4GB 环境下的稳定并发能力          |
| **内存占用**   | 80-120 MB   | 空闲到中等负载状态                   |

> 以上数据为架构分析预估值，建议使用 wrk 或 ab 工具进行实际压测验证。

## 📋 技术栈详情

### 后端技术栈

| 组件              | 技术选型                     | 版本                 | 用途               |
| --------------- | ------------------------ | ------------------ | ---------------- |
| **编程语言**        | C++17                    | ISO/IEC 14882:2017 | 高性能后端            |
| **Web 框架**      | Drogon                   | 1.x                | 高性能异步 HTTP 框架    |
| **数据库**         | MySQL                    | 8.0+               | 元数据存储            |
| **对象存储**        | MinIO                    | Latest             | 分布式文件存储          |
| **缓存**          | Redis                    | 7.x+               | 会话和缓存            |
| **Redis C 客户端** | hiredis                  | -                  | Redis 底层通信        |
| **图像处理**        | libvips                  | 8.0+               | 高性能缩略图生成         |
| **JSON 解析**     | RapidJSON                | 1.1.0              | JSON 序列化/反序列化    |
| **连接池**         | 自定义实现                    | -                  | MySQL/Redis 连接管理 |
| **异步任务**        | 并发队列                     | 自定义                | 异步处理             |
| **日志系统**        | 异步日志                     | 自定义                | 结构化日志            |
| **INI 解析**      | INIReader (inih)         | -                  | 配置文件解析           |
| **XML 解析**      | pugixml                  | -                  | XML 处理           |
| **C++ CURL 封装** | curlpp                   | -                  | HTTP 客户端封装       |

### 前端技术栈

| 组件           | 技术选型               | 版本  | 用途     |
| ------------ | ------------------ | --- | ------ |
| **框架**       | Vue.js (CDN)       | 3.x | 前端应用   |
| **UI 组件**    | Element Plus (CDN) | 2.x | 现代化 UI |
| **HTTP 客户端** | Axios (CDN)        | 1.x | API 请求 |
| **图标库**      | Font Awesome (CDN) | 6.x | 矢量图标   |

## 🚀 快速开始

### 方法一：Docker 一键部署（推荐）

> 适用于全新服务器，从安装 Docker 到启动服务，全程约 10\~20 分钟。

#### 第 1 步：安装 Docker

```bash
# 方式 A：官方脚本安装（推荐，适用于 Ubuntu / Debian）
# 如果服务器能访问外网，直接执行：
curl -fsSL https://get.docker.com | sh
sudo systemctl enable --now docker

# 方式 B：如果 curl 超时（国内网络问题），用阿里云镜像源安装：
curl -fsSL https://mirrors.aliyun.com/docker-ce/linux/ubuntu/gpg | sudo apt-key add -
sudo add-apt-repository "deb [arch=amd64] https://mirrors.aliyun.com/docker-ce/linux/ubuntu $(lsb_release -cs) stable"
sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io
sudo systemctl enable --now docker
```

验证 Docker 已安装：

```bash
docker --version
# 应输出类似：Docker version 24.x.x, build xxxxxxx
```

> **常见问题**: 如果提示 `/usr/bin/docker: No such file or directory`，说明 Docker 未安装成功，请用方式 B 重装。如果提示 `Unit docker.service is masked`，执行 `sudo systemctl unmask docker && sudo systemctl enable --now docker`。

#### 第 2 步：安装 Docker Compose 插件

```bash
sudo apt-get install -y docker-compose-plugin

# 验证（应输出版本号）
docker compose version
# 应输出类似：Docker Compose version v2.x.x
```

> **注意**: Docker Compose 有两种版本——V2 插件（`docker compose`，推荐）和 V1 独立程序（`docker-compose`，已停止维护）。本项目使用 V2 语法。如果 `docker compose` 不可用，请确认已执行上面的安装命令。

#### 第 3 步：配置国内镜像加速（国内服务器必做）

> Docker Hub 在国内经常超时，不配置加速会导致拉取镜像失败。

```bash
# 配置镜像加速
sudo mkdir -p /etc/docker
sudo tee /etc/docker/daemon.json << 'EOF'
{
  "registry-mirrors": [
    "https://docker.1ms.run",
    "https://docker.xuanyuan.me"
  ]
}
EOF

# 重启 Docker 使配置生效
sudo systemctl daemon-reload
sudo systemctl restart docker

# 验证加速已生效（输出中应包含 "Registry Mirrors"）
docker info | grep -A 5 "Registry Mirrors"
```

> 如果上述镜像源不可用，可替换为其他可用源。阿里云用户可登录 [cr.console.aliyun.com](https://cr.console.aliyun.com) → 镜像工具 → 获取专属加速地址。

#### 第 4 步：部署项目

```bash
# 1. 克隆项目
git clone https://github.com/ywx914705/AeroImageHost.git
cd AeroImageHost

# 2. 复制环境配置文件并修改密码（必须执行！）
cp .env.example .env
# 编辑 .env 文件，将所有 CHANGE_ME_xxx 替换为强密码
nano .env
```

`.env` 文件示例（**请务必修改所有密码！**）：

```bash
# MySQL
MYSQL_ROOT_PASSWORD=YourStrongRootPassword123!
MYSQL_DATABASE=imagehost
MYSQL_USER=aero_user
MYSQL_PASSWORD=YourStrongAeroPassword123!

# MinIO
MINIO_ROOT_USER=minioadmin
MINIO_ROOT_PASSWORD=YourStrongMinioPassword123!
```

```bash
# 3. 复制 Docker 配置文件并修改（至少：`redis.password`、`minio.public_url`）
cp config/config-docker.example.json config/config-docker.json
nano config/config-docker.json
```

`config/config-docker.json` 关键修改项：

- **`redis.password`**：必须与项目根目录 `.env` 中的 `REDIS_PASSWORD` 完全一致（Docker Compose 中 Redis 使用 `requirepass`）。
- **`minio.public_url`**：将 `localhost` 换成你的服务器公网 IP 或域名（含端口），以便外链与预览正常。

```json
{
  "redis": {
    "host": "redis",
    "port": 6379,
    "pool_size": 16,
    "password": "YourStrongRedisPassword123!"
  },
  "minio": {
    "endpoint": "http://minio:9000",
    "presign_endpoint": "",
    "access_key": "minioadmin",
    "secret_key": "YourStrongMinioPassword123!",
    "bucket": "images",
    "public_url": "http://你的服务器IP:8082/api/i/"
  }
}
```

```bash
# 4. 启动所有服务（Nginx + MySQL + Redis + MinIO + App）
#    首次需要构建镜像，约 5~15 分钟，后续启动约 1 分钟
docker compose up -d

# 5. 查看服务状态（等待所有服务 healthy）
docker compose ps

# 6. 查看构建/运行日志（遇到问题时排查）
docker compose logs -f app
```

#### 第 5 步：访问服务

| 服务            | 地址                    | 说明                                            |
| ------------- | --------------------- | --------------------------------------------- |
| **Web 界面**    | `http://你的服务器IP:8082` | 图床主界面                                         |
| **监控面板**    | `http://你的服务器IP:8082/monitor/dashboard.html` | 独立仪表盘页面（调用 `/api/metrics`，需先在主站登录使 `localStorage` 含 token） |
| **MinIO 控制台** | `http://你的服务器IP:9090` | 对象存储管理（用户名: `minioadmin`，密码: `.env` 中设置的 `MINIO_ROOT_PASSWORD`） |

#### 常用运维命令

```bash
docker compose ps              # 查看服务状态
docker compose logs -f app     # 查看应用日志
docker compose logs -f nginx   # 查看 Nginx 日志
docker compose logs -f db      # 查看数据库日志
docker compose restart app     # 重启应用
docker compose down            # 停止所有服务
docker compose down -v         # 停止并删除数据卷（危险！会清除所有数据）
docker compose build           # 重新构建镜像
docker compose exec app bash   # 进入应用容器调试
```

#### 常见问题排查

| 问题                                           | 原因                | 解决方案                                                                                        |
| -------------------------------------------- | ----------------- | ------------------------------------------------------------------------------------------- |
| `docker compose up` 卡在拉取镜像                   | 国内无法访问 Docker Hub | 执行第 3 步配置镜像加速                                                                               |
| `docker: 'compose' is not a docker command`  | 未安装 Compose 插件    | 执行第 2 步安装 `docker-compose-plugin`                                                           |
| 启动后 app 容器退出                                 | 配置错误或依赖未就绪        | `docker compose logs app` 查看具体报错                                                            |
| `libINIReader.so: cannot open shared object` | 运行时缺少 inih 库      | 确保使用最新代码（`git pull`），Dockerfile 已修复                                                         |
| 页面 404 或白屏                                   | Nginx 未正确代理       | 确认 `www/` 目录存在且包含 `index.html`，检查 `docker compose logs nginx`                               |
| 从其他机器无法访问                                    | `public_url` 配置错误 | 修改 `config/config-docker.json` 中 `minio.public_url` 为服务器 IP，然后 `docker compose restart app` |

### 方法二：本地编译部署

#### 系统要求

- **操作系统**: Linux (Ubuntu 20.04+/CentOS 8+), macOS, Windows (WSL2)
- **编译器**: GCC 7+, Clang 6+, MSVC 2017+
- **内存**: 最低 2GB，推荐 4GB+
- **磁盘**: 至少 10GB 可用空间

#### 安装步骤

```bash
# 1. 安装系统依赖
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config git \
    libssl-dev libcurl4-openssl-dev libmysqlclient-dev libvips-dev \
    libhiredis-dev libcurlpp-dev libpugixml-dev libinih-dev \
    nlohmann-json3-dev zlib1g-dev libjsoncpp-dev uuid-dev libbrotli-dev

# 2. 为 miniocpp 创建 CMake config 文件（Ubuntu apt 包不提供 vcpkg 风格的 config）
sudo mkdir -p /usr/lib/cmake/unofficial-curlpp
printf 'include(CMakeFindDependencyMacro)\nadd_library(unofficial::curlpp::curlpp SHARED IMPORTED)\nset_target_properties(unofficial::curlpp::curlpp PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libcurlpp.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n  INTERFACE_LINK_LIBRARIES "CURL::libcurl"\n)\n' | sudo tee /usr/lib/cmake/unofficial-curlpp/unofficial-curlpp-config.cmake > /dev/null

sudo mkdir -p /usr/lib/cmake/unofficial-inih
printf 'add_library(unofficial::inih::inih SHARED IMPORTED)\nset_target_properties(unofficial::inih::inih PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libinih.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\nadd_library(unofficial::inih::inireader SHARED IMPORTED)\nset_target_properties(unofficial::inih::inireader PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libinih.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n  INTERFACE_LINK_LIBRARIES "unofficial::inih::inih"\n)\n' | sudo tee /usr/lib/cmake/unofficial-inih/unofficial-inih-config.cmake > /dev/null

sudo mkdir -p /usr/lib/cmake/nlohmann_json
printf 'include(CMakeFindDependencyMacro)\nadd_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)\nset_target_properties(nlohmann_json::nlohmann_json PROPERTIES\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\n' | sudo tee /usr/lib/cmake/nlohmann_json/nlohmann_jsonConfig.cmake > /dev/null

sudo mkdir -p /usr/lib/cmake/pugixml
printf 'add_library(pugixml::pugixml SHARED IMPORTED)\nset_target_properties(pugixml::pugixml PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libpugixml.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\nadd_library(pugixml::pugixml-static STATIC IMPORTED)\nset_target_properties(pugixml::pugixml-static PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libpugixml.a"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include"\n)\n' | sudo tee /usr/lib/cmake/pugixml/pugixmlConfig.cmake > /dev/null

# 3. 编译安装 Drogon（Web 框架；CMake 通过 `find_package(Drogon)` 查找，默认安装到 /usr/local）
git clone --depth 1 --branch v1.9.4 https://github.com/drogonframework/drogon.git /tmp/drogon && \
  cd /tmp/drogon && git submodule update --init --recursive && \
  mkdir build && cd build && \
  cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF .. && \
  make -j$(nproc) && sudo make install && \
  cd / && sudo rm -rf /tmp/drogon && sudo ldconfig

# 4. 编译安装 miniocpp（MinIO C++ SDK，Ubuntu 源中没有）
#    直连 GitHub，失败则走 ghproxy 镜像
(curl -L -o /tmp/minio-cpp.tar.gz \
       https://github.com/minio/minio-cpp/archive/refs/heads/master.tar.gz \
       --retry 3 --retry-delay 10 --max-time 120 \
 || \
 curl -L -o /tmp/minio-cpp.tar.gz \
       https://ghproxy.com/https://github.com/minio/minio-cpp/archive/refs/heads/master.tar.gz \
       --retry 3 --retry-delay 10 --max-time 120) && \
tar -xzf /tmp/minio-cpp.tar.gz -C /tmp && \
mv /tmp/minio-cpp-* /tmp/minio-cpp && \
cd /tmp/minio-cpp && \
# 修补 CURL::libcurl 缺失问题
printf 'add_library(CURL::libcurl SHARED IMPORTED)\nset_target_properties(CURL::libcurl PROPERTIES\n  IMPORTED_LOCATION "/usr/lib/x86_64-linux-gnu/libcurl.so"\n  INTERFACE_INCLUDE_DIRECTORIES "/usr/include")\n' > /tmp/curl_fix.cmake && \
sed -i '1i\include("/tmp/curl_fix.cmake")' CMakeLists.txt && \
# 修补 pugixml API 兼容性（set_value(value) → set_value(value.c_str())）
sed -i 's/doc\.append_child(pugi::node_pcdata)\.set_value(value);/doc.append_child(pugi::node_pcdata).set_value(value.c_str());/' src/utils.cc && \
mkdir build && cd build && \
cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc) && sudo make install && \
rm -rf /tmp/minio-cpp /tmp/minio-cpp.tar.gz /tmp/curl_fix.cmake

# 5. 数据库初始化
mysql -u root -p < schema/01_init.sql
mysql -u root -p < schema/02_email_verification.sql

# 6. 编译项目
cd AeroImageHost
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON ..
make -j$(nproc)
ctest --output-on-failure

# 7. 创建配置文件
cat > ../config.json << EOF
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
  "security": {
    "cors_origin": "http://localhost:8082",
    "max_login_attempts": 5,
    "max_login_attempts_per_ip": 30,
    "login_window_seconds": 900,
    "max_send_code_requests_per_ip_per_hour": 10,
    "send_code_ip_window_seconds": 3600,
    "admin_user_id": 1
  },
  "files_list": {
    "max_page_size": 100,
    "max_offset": 5000000,
    "max_search_keyword_length": 128
  },
  "cleanup": {
    "multipart_ttl_seconds": 86400
  },
  "minio": {
    "endpoint": "http://localhost:9000",
    "access_key": "minioadmin",
    "secret_key": "minioadmin123",
    "bucket": "images",
    "public_url": "http://localhost:8082/api/i/"
  },
  "smtp": {
    "server": "smtp.qq.com",
    "port": 465,
    "username": "your_email@qq.com",
    "password": "your_smtp_authorization_code",
    "from": "your_email@qq.com"
  },
  "log": {
    "file": "./logs/server.log",
    "flush_interval": 3
  }
}
EOF

# 8. 启动服务
./AeroImageHost ../config.json
```

### 方法三：Nginx 反向代理（生产推荐）

本地编译或 Docker 部署后，建议使用 Nginx 反向代理，提供 HTTPS、静态资源缓存、请求限制等功能。

```nginx
server {
    listen 80;
    server_name your-domain.com;
    client_max_body_size 100M;  # 限制上传文件大小

    # 静态资源缓存
    location /assets/ {
        alias /path/to/AeroImageHost/www/assets/;
        expires 30d;
        add_header Cache-Control "public, immutable";
    }

    # API 反向代理（上传文件需要更长超时）
    location /api/ {
        proxy_pass http://127.0.0.1:8082/api/;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_read_timeout 300s;    # 大文件上传+MinIO处理需要更长时间
        proxy_send_timeout 300s;
        proxy_connect_timeout 10s;
        proxy_buffering off;         # 上传时关闭缓冲，避免内存堆积
    }

    # 前端页面
    location / {
        root /path/to/AeroImageHost/www;
        try_files $uri /index.html;
    }
}
```

> **提示**: 如需 HTTPS，可通过 Let's Encrypt 申请免费证书，或使用 Cloudflare 等 CDN 服务。

> **⚠️ Cloudflare 用户注意**: Cloudflare 免费版对 POST 请求有 **100MB 限制**和 **100 秒超时**。上传大文件时可能触发 502/524 错误。解决方案：
>
> 1. 大文件 (>100MB) 必须使用分片上传（前端已自动处理）
> 2. 在 Cloudflare Dashboard → Network → Timeouts 中将 `HTTP Rise Timeouts` 调大
> 3. 或在 Nginx 层面直接处理上传，绕过 Cloudflare 代理

## 📖 API 文档

### API 概览

所有 API 均以 `/api` 为前缀，返回 JSON 格式数据。支持 CORS 跨域请求。

### 认证接口

| 方法     | 端点                         | 描述      | 是否需要认证 |
| ------ | -------------------------- | ------- | ------ |
| `POST` | `/api/auth/register`       | 普通账号注册  | 否      |
| `POST` | `/api/auth/login`          | 用户登录    | 否      |
| `POST` | `/api/auth/send-code`      | 发送邮箱验证码 | 否      |
| `POST` | `/api/auth/email-register` | 邮箱验证码注册 | 否      |

#### 示例：用户注册

```bash
curl -X POST http://localhost:8082/api/auth/register \
  -H "Content-Type: application/json" \
  -d '{"account": "testuser", "password": "password123"}'
```

**响应：**

```json
{"status": "success"}
```

#### 示例：用户登录

```bash
curl -X POST http://localhost:8082/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"account": "testuser", "password": "password123"}'
```

**响应：**

```json
{
  "token": "a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6",
  "user_id": 1,
  "account": "testuser"
}
```

### 文件操作接口

| 方法         | 端点                                     | 描述                    | 是否需要认证 |
| ---------- | -------------------------------------- | --------------------- | ------ |
| `POST`     | `/api/upload?filename=xxx`             | 直接上传文件                | 是      |
| `POST`     | `/api/upload/presign`                  | 请求预签名上传 URL           | 是      |
| `POST`     | `/api/upload/confirm`                  | 确认预签名上传完成             | 是      |
| `POST`     | `/api/upload/multipart/init`           | 初始化分片上传               | 是      |
| `POST`     | `/api/upload/multipart/chunk`          | 上传单个分片（服务器代理）         | 是      |
| `POST`     | `/api/upload/multipart/complete`       | 合并分片完成上传              | 是      |
| `POST`     | `/api/upload/multipart/cleanup`        | 清理未完成的分片              | 是      |
| `GET`      | `/api/file/{file_id}/presign`          | 按需获取文件预签名 URL         | 是      |
| `GET`      | `/api/files?offset=0&limit=20&search=` | 获取文件列表                | 是      |
| `DELETE`   | `/api/file/{file_id}`                  | 删除文件                  | 是      |
| `POST`     | `/api/files/batch-delete`              | 批量删除文件                | 是      |
| `PUT`      | `/api/file/{file_id}/public`           | 切换文件公开/私有             | 是      |
| `GET`      | `/api/share/{file_id}`                 | 获取文件分享信息              | 否      |
| `GET`      | `/api/i/{file_id}?size=N`              | 访问文件（支持缩略图）           | 否\*    |
| `GET`      | `/api/stats`                           | 获取系统统计（Redis 短期缓存，见 `stats_cache_ttl_seconds`；上传/删除/注册等写入后失效） | 是      |
| `GET`      | `/api/health`                          | 深度健康检查（mysql/redis/minio） | 否      |
| `GET`      | `/api/metrics`                         | JSON 指标（MetricsCollector） | 是      |
| `POST`     | `/api/cleanup`                         | 清理孤儿文件（管理员）           | 是      |

> \* 公开文件无需认证，私有文件需要认证

#### 示例：上传文件

```bash
# 直接上传
curl -X POST "http://localhost:8082/api/upload?filename=photo.jpg" \
  -H "Authorization: Bearer your_token_here" \
  -H "Content-Type: image/jpeg" \
  --data-binary "@photo.jpg"

# 预签名上传（推荐大文件）
curl -X POST http://localhost:8082/api/upload/presign \
  -H "Authorization: Bearer your_token_here" \
  -H "Content-Type: application/json" \
  -d '{"filename": "large_file.zip", "size": 52428800}'
```

**响应：**

```json
{
  "file_id": "550e8400-e29b-41d4-a716-446655440000",
  "filename": "photo.jpg",
  "size": 1048576,
  "mime_type": "image/jpeg",
  "download_url": "http://localhost:8082/api/i/550e8400-e29b-41d4-a716-446655440000"
}
```

#### 示例：获取文件列表

```bash
curl "http://localhost:8082/api/files?offset=0&limit=20&search=photo" \
  -H "Authorization: Bearer your_token_here"
```

**响应：**

```json
{
  "files": [
    {
      "file_id": "uuid",
      "filename": "photo.jpg",
      "size": 1024000,
      "mime_type": "image/jpeg",
      "width": 1920,
      "height": 1080,
      "upload_time": 1714464000,
      "is_public": false,
      "view_count": 0,
      "download_url": "http://localhost:8082/api/i/uuid",
      "needs_preview": false
    },
    {
      "file_id": "uuid2",
      "filename": "video.mp4",
      "size": 52428800,
      "mime_type": "video/mp4",
      "width": 0,
      "height": 0,
      "upload_time": 1714464000,
      "is_public": false,
      "view_count": 5,
      "download_url": "http://localhost:8082/api/i/uuid2",
      "needs_preview": true
    }
  ],
  "total": 42
}
```

#### 示例：批量删除文件

```bash
curl -X POST http://localhost:8082/api/files/batch-delete \
  -H "Authorization: Bearer your_token_here" \
  -H "Content-Type: application/json" \
  -d '{"file_ids": ["uuid1", "uuid2", "uuid3"]}'
```

**响应：**

```json
{
  "status": "success",
  "deleted_count": 3,
  "failed_count": 0
}
```

#### 示例：分片上传（断点续传）

```bash
# 步骤1：初始化分片上传
curl -X POST http://localhost:8082/api/upload/multipart/init \
  -H "Authorization: Bearer your_token_here" \
  -H "Content-Type: application/json" \
  -d '{"filename": "large_video.mp4", "content_type": "video/mp4", "size": 52428800}'

# 响应：
# {
#   "upload_id": "uuid",
#   "chunk_size": 5242880,
#   "total_chunks": 10,
#   "chunks": [
#     {"part_number": 0, "presign_url": "https://minio.../chunks/uuid/0?..."},
#     {"part_number": 1, "presign_url": "https://minio.../chunks/uuid/1?..."}
#   ]
# }

# 步骤2：逐片上传到 MinIO（前端自动处理，支持断点续传）

# 步骤3：合并分片
curl -X POST http://localhost:8082/api/upload/multipart/complete \
  -H "Authorization: Bearer your_token_here" \
  -H "Content-Type: application/json" \
  -d '{"upload_id": "uuid", "filename": "large_video.mp4", "content_type": "video/mp4", "size": 52428800}'
```

#### 示例：获取预签名 URL（按需）

```bash
# 打开文件详情时，前端按需获取 presign_url，用于 PDF/视频/音频直连 MinIO 预览
curl http://localhost:8082/api/file/550e8400-e29b-41d4-a716-446655440000/presign \
  -H "Authorization: Bearer your_token_here"
```

**响应：**

```json
{
  "file_id": "550e8400-e29b-41d4-a716-446655440000",
  "presign_url": "http://minio-host:9000/images/550e8400...?X-Amz-Algorithm=..."
}
```

#### 示例：访问文件

```bash
# 访问原始文件
curl http://localhost:8082/api/i/550e8400-e29b-41d4-a716-446655440000

# 访问缩略图（200x200）
curl "http://localhost:8082/api/i/550e8400-e29b-41d4-a716-446655440000?size=200"

# 下载文件（带原始文件名）
curl "http://localhost:8082/api/i/550e8400-e29b-41d4-a716-446655440000?download"
```

### 认证头格式

所有需要认证的请求都需要在 Header 中添加：

```
Authorization: Bearer {your_token}
```

## 🔧 配置文件说明

### config.json 结构

```json
{
  "http_port": 8082,
  "http_threads": 4,
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
    "presign_endpoint": "",
    "access_key": "minioadmin",
    "secret_key": "minioadmin123",
    "bucket": "images",
    "public_url": "http://localhost:8082/api/i/"
  },

  "security": {
    "cors_origin": "http://localhost:8082",
    "max_login_attempts": 5,
    "max_login_attempts_per_ip": 30,
    "login_window_seconds": 900,
    "max_send_code_requests_per_ip_per_hour": 10,
    "send_code_ip_window_seconds": 3600,
    "admin_user_id": 1
  },

  "files_list": {
    "max_page_size": 100,
    "max_offset": 5000000,
    "max_search_keyword_length": 128
  },

  "cleanup": {
    "multipart_ttl_seconds": 86400
  },

  "smtp": {
    "server": "smtp.qq.com",
    "port": 465,
    "secure": true,
    "username": "your_email@qq.com",
    "password": "your_smtp_authorization_code",
    "from": "your_email@qq.com"
  },

  "log": {
    "file": "./logs/server.log",
    "flush_interval": 3
  }
}
```

> **注意**: `smtp` 配置用于邮箱验证码注册功能。`password` 填写邮箱的 SMTP 授权码（非登录密码），QQ 邮箱可在「设置 → 账户 → POP3/SMTP 服务」中开启并获取。Docker 部署时 **`redis.password` 必须与 `.env` 中的 `REDIS_PASSWORD` 一致**（Compose 中 Redis 启用了 `requirepass`）。

### 自动化测试与 CI

- **单元测试**：`tests/forwarded_for_parse_test.cc`（`X-Forwarded-For` 首跳解析），构建时开启 `-DBUILD_TESTING=ON`，运行 `ctest --output-on-failure`。Docker 构建镜像阶段会执行 `ctest`。
- **CI**：仓库内 `.github/workflows/ci.yml`，在 PR / push 时执行 `docker compose build app`，验证完整依赖链（Drogon、miniocpp、主程序）。

### 生产部署提示

1. **HTTPS**：对外服务应在 Nginx / 云负载均衡上终结 TLS；当前示例 Compose 中 Nginx 仅监听 80。
2. **CORS**：将 `security.cors_origin` 设为前端真实源；生产环境不要使用 `*`。
3. **Redis**：若使用密码，`redis.password` 与运行环境（如 `.env` 的 `REDIS_PASSWORD`）必须一致，否则应用无法完成 `AUTH`。
4. **备份与演练**：定期备份 MySQL 与 MinIO 数据卷，并在低峰期做恢复演练。

### 配置项说明

| 配置项                             | 类型      | 默认值                            | 描述                                        |
| ------------------------------- | ------- | ------------------------------ | ----------------------------------------- |
| `http_port`                     | integer | 8082                           | HTTP 服务监听端口                               |
| `http_threads`                  | integer | 4                              | Drogon I/O 线程数（默认 CPU 核心数）                 |
| `max_file_size`                 | integer | 104857600                      | 单文件最大字节数 (100MB)                          |
| `stats_cache_ttl_seconds`       | integer | 60                             | `GET /api/stats` 的 Redis JSON 缓存 TTL（秒）；`0` 关闭；写入后主动失效        |
| `mysql.host`                    | string  | localhost                      | MySQL 主机地址                                |
| `mysql.port`                    | integer | 3306                           | MySQL 端口                                  |
| `mysql.user`                    | string  | aero\_user                     | MySQL 用户名                                 |
| `mysql.password`                | string  | -                              | MySQL 密码                                  |
| `mysql.db`                      | string  | imagehost                      | 数据库名称                                     |
| `mysql.pool_size`               | integer | 32                             | MySQL 连接池大小                               |
| `redis.host`                    | string  | 127.0.0.1                      | Redis 主机地址                                |
| `redis.port`                    | integer | 6379                           | Redis 端口                                  |
| `redis.pool_size`               | integer | 16                             | Redis 连接池大小                               |
| `redis.password`                | string  | ""                             | Redis `AUTH` 密码；Docker 下须与 `.env` 的 `REDIS_PASSWORD` 一致 |
| `minio.endpoint`                | string  | <http://localhost:9000>        | MinIO 服务地址（需包含 http\:// 或 https\:// 前缀）   |
| `minio.presign_endpoint`        | string  | ""                             | 预签名 URL 使用的外部地址（为空则与 endpoint 相同，可用于 CDN） |
| `minio.access_key`              | string  | minioadmin                     | MinIO 访问密钥                                |
| `minio.secret_key`              | string  | minioadmin123                  | MinIO 密钥                                  |
| `minio.bucket`                  | string  | images                         | MinIO 存储桶名称                               |
| `minio.public_url`              | string  | <http://localhost:8082/api/i/> | 文件公开访问 URL 前缀                             |
| `security.cors_origin`          | string  | \*                             | CORS 允许的域名（生产环境请设为具体源，勿用 `*`）              |
| `security.max_login_attempts`   | integer | 5                              | 同一账号在窗口期内允许的最大登录失败次数                         |
| `security.max_login_attempts_per_ip` | integer | 30                        | 同一客户端 IP 在窗口期内允许的最大登录失败次数                    |
| `security.login_window_seconds` | integer | 900                            | 登录频率限制：时间窗口（秒，默认 15 分钟）                   |
| `security.max_send_code_requests_per_ip_per_hour` | integer | 10           | 同一 IP 在 `send_code_ip_window_seconds` 内最多请求发码次数   |
| `security.send_code_ip_window_seconds` | integer | 3600                    | 发验证码 IP 限流窗口（秒）                              |
| `security.admin_user_id`        | integer | 1                              | 管理员用户 ID（用于 /api/cleanup 接口权限控制）            |
| `files_list.max_page_size`      | integer | 100                            | `GET /api/files` 单次 `limit` 上限（1–500）              |
| `files_list.max_offset`         | integer | 5000000                        | `GET /api/files` 的 `offset` 上限（防深分页滥用）            |
| `files_list.max_search_keyword_length` | integer | 128                     | `search` 查询参数最大长度（超出截断；`0` 表示不限制）              |
| `cleanup.multipart_ttl_seconds` | integer | 86400                          | 分片上传孤儿清理超时时间（秒，默认 24 小时）                  |
| `log.file`                      | string  | ./logs/server.log              | 日志文件路径                                    |
| `log.flush_interval`            | integer | 3                              | 日志刷新间隔（秒）                                 |
| `smtp.server`                   | string  | smtp.qq.com                    | SMTP 服务器地址                                |
| `smtp.port`                     | integer | 465                            | SMTP 端口（465 为 SSL，587 为 STARTTLS）         |
| `smtp.username`                 | string  | -                              | 发件人邮箱地址                                   |
| `smtp.password`                 | string  | -                              | 邮箱 SMTP 授权码（非登录密码）                        |
| `smtp.from`                     | string  | -                              | 发件人显示邮箱（通常与 username 相同）                  |
| `smtp.secure`                   | boolean | true                           | 是否使用 SSL 连接（端口 465 设 true，587 设 false）     |

## 🗃️ 数据库设计

### users 表 - 用户信息

```sql
CREATE TABLE users (
    id INT NOT NULL AUTO_INCREMENT,
    account VARCHAR(64) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    email VARCHAR(255) DEFAULT NULL,
    created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY account (account),
    UNIQUE KEY email (email)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
```

### files 表 - 文件元数据

```sql
CREATE TABLE files (
    id BIGINT NOT NULL AUTO_INCREMENT,
    file_id VARCHAR(64) NOT NULL,
    user_id INT NOT NULL,
    filename VARCHAR(512) NOT NULL,
    size BIGINT NOT NULL DEFAULT 0,
    mime_type VARCHAR(256) DEFAULT NULL,
    width INT DEFAULT 0,
    height INT DEFAULT 0,
    upload_time BIGINT NOT NULL,
    is_public TINYINT(1) DEFAULT 0,
    view_count BIGINT DEFAULT 0,
    allow_domains VARCHAR(512) DEFAULT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY file_id (file_id),
    KEY user_id (user_id),
    KEY upload_time (upload_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
```

> **注意**:
>
> - `allow_domains` 字段存在于数据库中，当前版本代码尚未使用此字段（预留扩展）
> - `FileMeta.hpp` 中定义了 `md5` 字段，但数据库表和代码中均未使用（死代码，建议后续清理）

### email\_verifications 表 - 邮箱验证码

```sql
CREATE TABLE email_verifications (
    id INT NOT NULL AUTO_INCREMENT,
    email VARCHAR(255) NOT NULL,
    verification_code VARCHAR(10) NOT NULL,
    expires_at TIMESTAMP NOT NULL,
    created_at TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP,
    used TINYINT(1) DEFAULT 0,
    PRIMARY KEY (id),
    INDEX idx_email (email),
    INDEX idx_code (verification_code),
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
```

## 📁 项目结构

```
AeroImageHost/
├── CMakeLists.txt               # CMake 构建配置
├── Dockerfile                   # Docker 镜像构建
├── docker-compose.yml           # Docker 服务编排（含 Nginx 代理）
├── nginx.conf                   # Nginx 反向代理配置
├── .gitignore                   # Git 忽略规则
├── config.json                  # 本地开发配置（.gitignore 排除，不提交到 Git）
├── main.cc                      # 主程序入口
│
├── config/                      # 配置模块
│   ├── Config.cc                # 配置加载器
│   ├── Config.hpp
│   └── config-docker.example.json # Docker 环境配置示例
│
├── http/                        # HTTP 服务层
│   ├── Controllers/             # Drogon 路由控制器
│   │   ├── AuthController.h/cc  # 认证路由（注册、登录、邮箱验证码）
│   │   ├── FileController.h/cc  # 文件操作路由（列表、删除、分享、公开切换）
│   │   ├── UploadController.h/cc # 上传路由（直接、预签名、分片）
│   │   └── SystemController.h/cc # 系统路由（健康检查、统计、监控指标）
│   ├── Filters/                 # Drogon 请求过滤器
│   │   └── AuthFilter.h/cc     # Bearer Token 认证过滤器
│   ├── Handlers.cc              # 业务逻辑处理函数（无状态全局函数）
│   ├── Handlers.hpp
│   ├── Auth.cc                  # Token 认证（两级缓存验证）
│   └── Auth.hpp
│
├── storage/                     # 数据访问层（每张表一个 DAO）
│   ├── FileMeta.cc              # files 表 DAO（文件元数据 CRUD）
│   ├── FileMeta.hpp
│   ├── UsersDAO.cc              # users 表 DAO（注册、登录、邮箱检查）
│   ├── UsersDAO.hpp
│   ├── MinIOClient.cc           # MinIO SDK 客户端
│   ├── MinIOClient.hpp
│   ├── EmailVerificationDAO.cc  # email_verifications 表 DAO（验证码管理）
│   └── EmailVerificationDAO.hpp
│
├── image/                       # 图像处理
│   ├── ImageProcessor.cc        # VIPS 缩略图生成
│   └── ImageProcessor.hpp
│
├── src/                         # 基础设施
│   ├── AeroQueue.cc             # 异步任务队列实现
│   ├── ConnectionPool.cc        # MySQL 连接池实现
│   ├── Log.cc                   # 异步日志系统实现
│   └── RedisClient.cc           # Redis 客户端实现
│
├── utils/                       # 工具类
│   ├── Utils.cc                 # URL 编解码、UUID 生成等
│   ├── Utils.hpp
│   ├── EmailSender.cc           # 邮件发送（SMTP）
│   ├── EmailSender.hpp
│   ├── PasswordHash.cc          # PBKDF2 密码哈希
│   ├── PasswordHash.hpp
│   ├── RateLimiter.cc           # 基于 Redis 的登录频率限制
│   └── RateLimiter.hpp
│
├── monitor/                     # 监控指标
│   ├── MetricsCollector.cc      # 系统指标采集（CPU、内存、QPS）
│   └── MetricsCollector.hpp
│
├── include/                     # 基础设施头文件
│   ├── AeroQueue.hpp            # 异步任务队列头文件
│   ├── ConnectionPool.hpp       # 连接池接口
│   ├── Log.hpp                  # 日志系统头文件
│   ├── RedisClient.hpp          # Redis 客户端头文件
│   ├── DbGuard.hpp              # 数据库连接 RAII 守卫
│   ├── concurrentqueue.hpp      # 无锁并发队列库
│   └── rapidjson/               # RapidJSON 库
│       ├── document.h           # JSON 文档解析
│       ├── writer.h             # JSON 写入
│       ├── schema.h             # JSON Schema 验证
│       └── ...                  # 其他 RapidJSON 组件
│
├── schema/                      # SQL 脚本（按数字前缀顺序执行）
│   ├── 01_init.sql              # 基础表结构（users, files）
│   ├── 02_email_verification.sql # 邮箱验证表
│   ├── 03_migrate.sql           # 迁移脚本
│   └── 04_clean_data.sql        # 数据清理脚本
│
└── www/                         # 前端页面
    ├── index.html               # Vue 3 SPA 入口 (使用在线 CDN 引入依赖)
    ├── logo.png                 # 网站 Logo
    ├── architecture.html        # 架构图展示页面
    ├── monitor/                 # 监控面板前端
    │   └── dashboard.html       # 实时监控仪表盘
    └── assets/                  # 前端静态资源备份（实际使用在线 CDN）
        ├── vue.global.prod.js   # Vue 3 生产版本（本地备份）
        ├── index.full.js        # Element Plus 完整版（本地备份）
        ├── element-plus.js      # Element Plus JS（本地备份）
        ├── element-plus.css     # Element Plus 样式（本地备份）
        ├── axios.min.js         # Axios HTTP 客户端（本地备份）
        ├── dayjs.min.js         # Day.js 时间处理（本地备份）
        ├── all.min.css          # Font Awesome 样式（本地备份）
        ├── font-awesome.css     # Font Awesome 自定义样式（本地备份）
        └── fa-*.woff2           # Font Awesome 字体文件（本地备份）
```

## 📈 性能优化详解

### 1. 连接池优化

- **定时连接验证**: 每 30 秒验证一次连接有效性 (`ensureValidConnection`)，避免每次获取连接都 `mysql_ping`，减少网络往返开销
- **归还免验证**: 连接归还时不验证，延迟到下次获取时按需执行，进一步减少 `mysql_ping` 次数
- **超时保护**: 连接获取超时设置为 5 秒，避免死锁
- **自动重连**: 连接失效时自动重建
- **泄漏预防**: 使用 RAII 模式确保连接释放

### 2. 按需预签名 URL

- **文件列表免签名**: `handleListFiles` 不再为每个预览文件生成 `presign_url`，改为返回 `needs_preview` 标记
- **按需获取**: 前端打开文件详情时才调用 `/api/file/{file_id}/presign` 获取预签名 URL
- **性能收益**: 文件列表接口耗时降低约 40%（每次省去 N 次 MinIO 签名计算）

### 3. HTTP 缓存优化

- **图片 ETag**: 图片响应携带 `ETag` + `Cache-Control: public, max-age=3600`，支持 304 Not Modified 条件请求
- **缩略图浏览器缓存**: 缩略图响应 `Cache-Control: public, max-age=86400`，24 小时浏览器缓存
- **缩略图 MinIO 缓存**: 生成的缩略图以 `thumbs/{file_id}_{size}` 为 key 存入 MinIO，后续请求直接从 MinIO 读取缓存，避免重复下载原图 + libvips 生成

### 4. 异步处理架构（AeroQueue）

- **无锁队列**: 基于 `concurrentqueue.hpp` 的 MPMC 无锁 FIFO 队列
- **工作线程**: 4 个工作线程消费异步任务（`main.cc` 中 `AeroQueue::instance().start(4)`）
- **缩略图缓存回写**: 缩略图生成后通过 `AeroQueue::instance().post()` 异步写入 MinIO `thumbs/` 前缀，不阻塞 HTTP 响应
- **邮件异步发送**: SMTP 邮件发送通过 AeroQueue fire-and-forget，消除 \~2s 的同步阻塞

### 5. 并行处理（std::async）

- **小文件直传 `handleUpload`**: 顺序为 **MinIO `putObject` → MySQL `save`**；若 MySQL 写入失败，会 **`deleteObject` 删除已上传对象**，避免脏数据。
- **批量删除并行化**: `handleBatchDeleteFiles` 中 N 个 MinIO `deleteObject` 通过 `std::async` 并行执行，缩短总耗时。

### 6. 内存管理

- **分片与大文件**: 大文件应使用分片上传；小文件直传受框架 `setClientMaxMemoryBodySize` 等限制，需合理设置 `max_file_size`。
- **RAII 资源管理**: 使用智能指针和 RAII 模式确保资源释放（如 `std::shared_ptr<UserInfo>`、`DbGuard` 数据库连接守卫）

### 7. 图像处理优化

- **libvips 优势**: 使用 libvips 替代 ImageMagick，内存占用减少 90%
- **缩略图缓存**: 生成的缩略图异步缓存到 MinIO `thumbs/` 前缀，后续请求直读缓存，避免重复生成
- **多格式支持**: 支持 JPEG、PNG、WebP 等多种格式

## 🔍 监控与日志

### 内置监控面板

系统内置实时监控面板，部署完成后访问：

| 端点 | 地址 | 说明 |
|------|------|------|
| **监控面板** | `http://你的服务器IP:8082/monitor/dashboard.html` | 独立页面：轮询 `GET /api/metrics`（**需登录**，与主站共用 `localStorage.token`） |
| **指标 JSON** | `http://你的服务器IP:8082/api/metrics` | `MetricsCollector` 聚合指标（需 `Authorization: Bearer`） |
| **健康检查** | `http://你的服务器IP:8082/api/health` | `mysql` / `redis` / `minio` 子状态；非全 ok 时返回 503 |
| **系统统计** | `http://你的服务器IP:8082/api/stats` | 文件数、用户数、存储量（需登录，与主站同一 Token） |

监控面板展示内容：
- **实时 QPS**：全局每秒请求数 + 历史趋势图
- **API 端点性能**：各接口请求数、平均延迟、P95 延迟、错误率
- **连接池状态**：MySQL 活跃/空闲连接数、Redis 连接数
- **缓存命中率**：Token 缓存、文件元数据缓存命中情况
- **系统运行时间**：服务启动时长

### 日志系统

- **异步写入**: 日志异步写入，不影响业务性能
- **多级别输出**: DEBUG、INFO、WARN、ERROR 四个级别
- **日志位置**: Docker 部署在 `/app/logs/server.log`（容器内），可通过 `docker compose logs -f app` 查看

### 健康检查

```bash
# 检查服务健康状态（MySQL / Redis / MinIO）
curl http://localhost:8082/api/health

# 响应示例（全部正常）
# {"mysql":"ok","redis":"ok","minio":"ok","status":"healthy"}
# 任一依赖异常时 status 为 degraded，HTTP 503

# 获取系统统计（需 Bearer Token）
curl -H "Authorization: Bearer YOUR_TOKEN" http://localhost:8082/api/stats
```

## 🎯 适用场景

### 👤 个人用户

- **博客图床**: 博客文章图片托管
- **个人相册**: 个人照片存储和分享
- **文档存储**: 个人文档和文件备份

### 👥 团队协作

- **项目文档**: 团队项目文档和图片
- **设计资源**: UI/UX 设计资源管理
- **开发工具**: 开发过程中的文件共享

### 🏢 企业应用

- **内容管理系统**: CMS 图片和文件存储
- **电商平台**: 商品图片托管
- **教育平台**: 教学资源管理
- **社交应用**: 用户上传内容存储

## 📅 开发路线图

### ✅ v1.0 已实现

- [x] 用户注册/登录系统
- [x] 双模式文件上传（直接 + 预签名）
- [x] 文件管理功能（列表、搜索、删除）
- [x] 图片缩略图生成（libvips）
- [x] Token 认证（Redis 会话）
- [x] MySQL + Redis 双连接池
- [x] Docker 一键部署
- [x] RESTful API 设计
- [x] 异步日志系统（双缓冲机制）
- [x] 异步任务队列 AeroQueue（4 工作线程 + 无锁 FIFO 队列）
- [x] 批量文件管理（多选、全选、批量删除）
- [x] 多格式文件预览（PDF/视频/音频在线预览）
- [x] 断点续传（分片上传 + 上传进度 + 页面刷新恢复）
- [x] 连接池定时验证优化（30s 间隔，归还免验证）
- [x] 按需预签名 URL（文件列表免签名，打开详情时按需获取）
- [x] HTTP 缓存优化（ETag + Cache-Control，304 条件请求）
- [x] 小文件上传一致性（MinIO 先写、MySQL 失败则删对象回滚）
- [x] 批量删除并行化（std::async：N 个 MinIO deleteObject 并行执行）
- [x] 缩略图缓存（MinIO thumbs/ 前缀 + AeroQueue 异步回写）
- [x] 邮件异步发送（AeroQueue fire-and-forget，消除 SMTP 阻塞）
- [x] 登录限流（同一账号 + 同一 IP，Redis 计数）
- [x] 邮箱验证码发送 IP 限流（防刷邮）
- [x] Redis `AUTH`（`redis.password` 与 Docker `requirepass` 一致）
- [x] 文件列表 `limit` / `offset` 上限（可配置）
- [x] 基础单元测试（`ForwardedForParse` + `ctest`，Docker 构建阶段执行）
- [x] GitHub Actions：`docker compose build app` 完整镜像构建 CI

### 🚧 v1.5 规划中

- [ ] 文件分类和标签系统
- [ ] 管理员面板增强
- [ ] 图片 EXIF 信息提取
- [ ] 用户配额管理
- [ ] WebP/AVIF 自动转换
- [ ] Redis 缓存文件元数据（减少 MySQL 查询）
- [ ] HTTP Range 请求（视频/音频分段加载）

### 📋 v2.0 计划中

- [ ] JWT Token 支持
- [ ] 多语言国际化
- [ ] CDN 集成支持
- [ ] 高级图片处理（水印、滤镜）
- [ ] 视频缩略图生成
- [ ] 分布式部署支持
- [ ] 实时监控面板

## 🤝 贡献指南

### 如何贡献

1. **Fork 项目**: 点击右上角 Fork 按钮
2. **创建分支**: `git checkout -b feature/your-feature-name`
3. **提交代码**: 遵循代码规范，添加测试
4. **创建 PR**: 提交 Pull Request，描述修改内容

### 代码规范

- **C++ 风格**: Google C++ Style Guide
- **命名规范**: 类名使用 CamelCase，函数/变量使用 snake\_case
- **注释要求**: 关键算法和复杂逻辑需要注释
- **提交信息**: 使用 Conventional Commits 格式

## 📚 学习资源

### 技术文档

- [C++17 标准](https://isocpp.org/std/the-standard)
- [Drogon 文档](https://drogon.docsforge.com/)
- [libvips 文档](https://libvips.github.io/libvips/)
- [MinIO 文档](https://docs.min.io/)
- [Redis 文档](https://redis.io/documentation)

### 相关项目

- [imgproxy](https://github.com/imgproxy/imgproxy) - 快速安全的图像处理服务
- [chevereto](https://chevereto.com/) - 图像托管软件
- [Lychee](https://lycheeorg.github.io/) - 开源照片管理工具
- [Piwigo](https://piwigo.org/) - 照片库软件

## 📝 常见问题

### Q1: 为什么选择 C++ 而不是其他语言？

**A**: C++ 提供接近硬件的高性能，内存占用低，适合处理大量图片和大文件。相比 Python 等语言，在相同硬件条件下可支持更高的并发量。

### Q2: 预签名 URL 有什么优势？

**A**: 预签名 URL 允许客户端直接上传到 MinIO，绕过服务器中转，大大减轻服务器负载，特别适合大文件上传和 CDN 集成。

### Q3: 如何保证文件安全性？

**A**: 1) 私有文件需要 Token 认证访问；2) 支持文件类型检测；3) 使用 UUID 作为文件名防止猜测；4) 建议通过 Nginx 反向代理启用 HTTPS。

### Q4: 支持哪些文件格式？

**A**: 支持图片（JPEG、PNG、GIF、WebP）、文档（PDF、DOC、DOCX、XLS、XLSX、PPT、PPTX、TXT）、视频（MP4、WebM、MOV、AVI）、音频（MP3、WAV、FLAC、OGG）、压缩包（ZIP、RAR、7Z）等多种格式。图片使用 libvips 处理，PDF/视频/音频支持在线预览。

### Q5: 如何扩展存储容量？

**A**: MinIO 支持集群部署，可通过添加更多节点水平扩展。数据库支持读写分离，可通过主从复制扩展。

### Q6: 是否有 API 限流？

**A**: 通过 Nginx 反向代理实现，配置 `client_max_body_size` 限制上传文件大小，同时可配置 `limit_req_zone` 实现请求频率限制。

## 📞 支持与反馈

### 问题反馈

1. **GitHub Issues**: 报告 Bug 或提出功能请求
2. **讨论区**: 参与技术讨论

## 📄 开源协议

本项目采用 **[MIT License](LICENSE)** 开源协议。

### 您可以

- ✅ 自由使用（包括商业用途）
- ✅ 修改源代码
- ✅ 分发软件及其源码

### 您必须

- ⚠️ 保留版权声明
- ⚠️ 包含许可证副本

## 🙏 致谢

感谢所有为 AeroImageHost 做出贡献的开发者！

### 核心技术栈致谢

- [libvips](https://github.com/libvips/libvips) - 高性能图像处理库
- [Drogon](https://github.com/drogonframework/drogon) - 高性能 C++ HTTP 框架
- [MinIO](https://min.io/) - 高性能对象存储
- [Redis](https://redis.io/) - 内存数据结构存储
- [RapidJSON](https://rapidjson.org/) - 快速 JSON 解析器

### 贡献者

<a href="https://github.com/ywx914705/AeroImageHost/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=ywx914705/AeroImageHost" />
</a>

***

<div align="center">

## 🚀 立即开始使用 AeroImageHost！

**高性能 · 现代化 · 易于部署**

[🐛 报告问题](https://github.com/ywx914705/AeroImageHost/issues) |
[💬 参与讨论](https://github.com/ywx914705/AeroImageHost/discussions)

[![GitHub stars](https://img.shields.io/github/stars/ywx914705/AeroImageHost?style=social)](https://github.com/ywx914705/AeroImageHost/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/ywx914705/AeroImageHost?style=social)](https://github.com/ywx914705/AeroImageHost/network/members)

</div>
