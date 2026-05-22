# AeroImageHost

<div align="center">
  <img src="www/assets/logo.png" alt="AeroImageHost" width="140" style="border-radius: 20px; box-shadow: 0 8px 24px rgba(0,0,0,0.15);">
  <br><br>
  <h2>高性能文件托管与图床平台</h2>
  <p>C++17 + Drogon · MinIO 分布式存储 · Redis 缓存 · Docker 一键部署</p>
  <p>支持 26 种文件格式 · 大文件断点续传 · 智能缩略图 · 在线预览</p>
  <br>

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg?logo=cplusplus)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.10+-064F8C.svg?logo=cmake)](https://cmake.org/)
[![MySQL 8.0](https://img.shields.io/badge/MySQL-8.0-4479A1.svg?logo=mysql)](https://www.mysql.com/)
[![MinIO S3](https://img.shields.io/badge/MinIO-S3-FE2C25.svg?logo=minio)](https://min.io/)
[![Redis](https://img.shields.io/badge/Redis-7.x-DC382D.svg?logo=redis)](https://redis.io/)
[![Docker](https://img.shields.io/badge/Docker-Ready-2496ED.svg?logo=docker)](https://www.docker.com/)
[![libvips](https://img.shields.io/badge/libvips-8.0+-f5a3b8.svg?logo=vips)](https://github.com/libvips/libvips)

[![GitHub stars](https://img.shields.io/github/stars/ywx914705/AeroImageHost?style=social)](https://github.com/ywx914705/AeroImageHost/stargazers)
[![GitHub forks](https://img.shields.io/github/stars/ywx914705/AeroImageHost?style=social)](https://github.com/ywx914705/AeroImageHost/network/members)

</div>

## 简介

AeroImageHost 是一个用 **C++17** 编写的高性能现代化图床系统，采用组件化单体架构设计，集成了 MySQL、MinIO、Redis 等技术栈。系统提供完整的用户认证、文件管理、图片处理和分布式存储功能，支持 Docker 一键部署，适用于个人、团队和企业级文件托管需求。

- **26 种文件格式**：图片、文档、视频、音频、压缩包全覆盖
- **极速性能**：缓存读取 39K+ QPS，认证列表 32K+ QPS，P50 < 6ms
- **智能上传**：拖拽、粘贴、断点续传，大文件自动分片
- **在线预览**：图片缩略图、PDF 全屏、视频播放、音频试听
- **安全可靠**：PBKDF2 加密、Token 鉴权、暴力破解防护、Nginx 三级限流
- **私有化部署**：Docker 一键部署，数据完全自主可控

## 界面预览

### 未登录页面

<div align="center">
  <img src="https://imagehost.aeroserver.ccwu.cc/api/i/0aa68d1d-afd0-48aa-b35c-600f26092375" alt="未登录页面" width="800" style="border-radius: 8px;">
  <br>
  <em>产品介绍与功能展示</em>
</div>

### 登录后页面

<div align="center">
  <img src="https://imagehost.aeroserver.ccwu.cc/api/i/84bce7bf-6b36-4e46-920a-e315cb6e4fce" alt="登录后页面" width="800" style="border-radius: 8px;">
  <br>
  <em>文件管理面板 — 上传、搜索、预览、分享</em>
</div>

## 核心能力

| 功能 | 说明 |
|------|------|
| 多格式上传 | 拖拽、粘贴、点击上传，大文件自动分片断点续传 |
| 文件管理 | 搜索、排序、批量操作，文件列表分页加载 |
| 在线预览 | 图片缩略图、PDF 全屏、视频播放、音频试听 |
| 一键分享 | 永久链接，支持私有/公开切换，域名白名单防盗链 |
| 一键 Markdown | 粘贴图片自动生成 Markdown 链接，写作更高效 |
| 安全可靠 | PBKDF2 加密、Token 鉴权、暴力破解防护、Nginx 三级限流 |
| 私有化部署 | Docker 一键部署，数据完全自主可控 |

## ⚡ 性能基准

> 详细压测报告见 [benchmark/BENCHMARK.md](benchmark/BENCHMARK.md)

| 接口 | 说明 | QPS | P50 | P95 | P99 |
|------|------|-----|-----|-----|-----|
| `GET /api/metrics` | 认证 + 原子计数器 | **43,479** | 3.2ms | 13.8ms | 21.4ms |
| `GET /api/stats` | Redis 缓存聚合 | **39,423** | 4.7ms | 9.1ms | 14.3ms |
| `GET /api/files` | 认证 + Redis HGETALL | **32,901** | 5.7ms | 10.3ms | 15.2ms |
| `GET /api/health` | MySQL + Redis + MinIO 深度检查 | **3,897** | 49.6ms | 84.7ms | 121.2ms |
| `POST /api/auth/login` | PBKDF2 (10万次迭代) | **67** | 218.6ms | 716.0ms | 922.7ms |
| `POST /api/upload` | MinIO 对象写入 | **101** | 13.1ms | 160.1ms | 160.1ms |

测试条件：16 工作线程 / 200 并发连接 / HTTP Keep-Alive / 10 秒持续压测

## 系统要求

### Docker 部署

| 项目 | 最低配置 | 推荐配置 |
|------|----------|----------|
| CPU | 2 核 | 4 核+ |
| 内存 | 2 GB | 4 GB+ |
| 磁盘 | 10 GB | 50 GB+（SSD 推荐） |
| 系统 | Ubuntu 20.04+ / CentOS 8+ | Ubuntu 22.04 LTS |
| Docker | 20.10+ | 最新稳定版 |
| Docker Compose | V2 插件 | 最新稳定版 |

> **实测环境**：4 核 4GB 内存 / 3Mbps 带宽的云服务器可稳定运行。

### 本地编译

| 项目 | 要求 |
|------|------|
| 编译器 | GCC 7+ / Clang 6+ |
| CMake | 3.10+ |
| 系统 | Linux (Ubuntu 20.04+) / macOS |
| 内存 | 2 GB+ |
| 磁盘 | 5 GB+（含编译依赖） |

## 快速部署

### Docker 部署（推荐）

```bash
git clone https://github.com/ywx914705/AeroImageHost.git
cd AeroImageHost
bash deploy-docker.sh
```

脚本会自动完成：配置环境变量 → 配置应用 → 构建镜像 → 启动服务 → 配置前端 → 等待就绪。

启动后访问 `http://你的服务器IP:8082`。

### 本地编译

```bash
git clone https://github.com/ywx914705/AeroImageHost.git
cd AeroImageHost
bash deploy-local.sh
```

脚本会自动完成：安装依赖 → 编译 Drogon/miniocpp → 初始化数据库 → 编译项目 → 创建配置。

详细部署步骤和常见问题请参考 [部署文档](docs/DEPLOYMENT.md)。

## 技术栈

| 组件 | 技术 | 用途 |
|------|------|------|
| 后端 | C++17 + Drogon | 高性能异步 HTTP 框架 |
| 数据库 | MySQL 8.0+ | 元数据存储 |
| 对象存储 | MinIO | 文件存储（S3 兼容） |
| 缓存 | Redis | 会话、缓存、限流 |
| 图像处理 | libvips | 缩略图生成 |
| 前端 | Vue 3 + Element Plus | SPA 用户界面 |
| 部署 | Docker Compose | 一键部署 |

## 项目结构

```
AeroImageHost/
├── CMakeLists.txt           # CMake 构建配置
├── Dockerfile               # Docker 镜像构建
├── docker-compose.yml       # Docker 服务编排
├── nginx.conf               # Nginx 反向代理配置
├── deploy-docker.sh         # Docker 一键部署脚本
├── deploy-local.sh          # 本地编译部署脚本
├── main.cc                  # 主程序入口
├── http/                    # HTTP 控制器和业务逻辑
├── storage/                 # 数据访问层（MySQL、MinIO）
├── image/                   # 图像处理（libvips）
├── src/                     # 基础设施（连接池、日志、队列）
├── include/                 # 头文件（连接池、Redis、队列等）
├── utils/                   # 工具类（加密、限流、邮件）
├── monitor/                 # 监控指标采集
├── config/                  # 配置模块
├── schema/                  # SQL 初始化脚本
├── benchmark/               # 性能基准测试
├── www/                     # 前端 SPA
└── docs/                    # 详细文档
```

## 相关链接

- [性能基准](benchmark/BENCHMARK.md) — 压测环境、方法与详细结果
- [详细文档](docs/DETAILED.md) — 架构设计、API 文档、配置说明
- [部署指南](docs/DEPLOYMENT.md) — Docker 和本地编译部署
- [GitHub Issues](https://github.com/ywx914705/AeroImageHost/issues) — 问题反馈

## 开源协议

[MIT License](LICENSE)
