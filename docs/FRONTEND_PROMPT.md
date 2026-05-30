# AeroImageHost 前端重写提示词

## 项目简介

AeroImageHost 是一个高性能图片与文件托管平台，使用 C++17 (Drogon) 后端 + Vue 3 前端。用户可以上传文件、管理文件、生成分享链接/Markdown 外链，并通过"图床大厅"社区功能分享图片。

## 技术栈

- **后端**: C++17, Drogon HTTP 框架, MySQL 8, Redis, MinIO (S3), libvips
- **前端**: Vue 3 (Composition API), Element Plus, Axios, dayjs, Font Awesome
- **部署**: Docker Compose (MySQL + Redis + MinIO + Nginx + App)
- **API 基础路径**: `/api/`

## 用户角色

- **普通用户**: 上传、管理自己的文件、发布到图床大厅
- **游客**: 只能访问公开文件的分享链接

---

## 完整功能列表

### 1. 认证系统

| 功能 | API | 说明 |
|------|-----|------|
| 注册 | `POST /api/auth/register` | 账号 + 密码注册，密码自动 bcrypt 加密 |
| 登录 | `POST /api/auth/login` | 返回 JWT token (24h 有效期) |
| 邮箱验证码 | `POST /api/auth/send-code` | 发送 6 位数字验证码到邮箱 |
| 邮箱注册 | `POST /api/auth/email-register` | 账号 + 密码 + 邮箱 + 验证码注册 |
| 登录限流 | - | 单账号 5 次/15 分钟，单 IP 30 次/15 分钟 |

### 2. 文件上传

| 功能 | API | 说明 |
|------|-----|------|
| 直接上传 | `POST /api/upload` | 小文件直接上传 (Content-Type: multipart/form-data) |
| 预签名上传 | `POST /api/upload/presign` | 获取预签名 URL，直传 MinIO (绕过后端代理) |
| 确认上传 | `POST /api/upload/confirm` | 预签名上传后确认，写入元数据 |
| 分片上传初始化 | `POST /api/upload/multipart/init` | 大文件分片上传 (每片 20MB) |
| 分片上传 | `PUT /api/upload/multipart/chunk` | 上传单个分片 |
| 分片完成 | `POST /api/upload/multipart/complete` | 合并分片，写入元数据 |
| 分片清理 | `POST /api/upload/multipart/cleanup` | 清理未完成的分片 |
| 最大文件 | - | 100MB (可配置) |
| 支持格式 | - | 图片(JPG/PNG/GIF/WebP/SVG), 文档(PDF/DOC/DOCX/PPT/XLS), 视频(MP4/WebM/MOV), 音频(MP3/WAV/FLAC), 压缩包(ZIP/RAR/7Z) |

### 3. 文件管理

| 功能 | API | 说明 |
|------|-----|------|
| 文件列表 | `GET /api/files` | 分页查询，支持 `search` (文件名搜索), `type` (image/document/video/audio), `sort` (time/size/name), `order` (asc/desc) |
| 文件详情 | `GET /api/i/{id}` | 通过 `?size=N` 获取缩略图，通过 `?download=1` 强制下载 |
| 删除文件 | `DELETE /api/file/{id}` | 删除文件及 MinIO 对象 |
| 批量删除 | `POST /api/files/batch-delete` | 批量删除 { file_ids: [...] } |
| 切换公开 | `PUT /api/file/{id}/public` | 切换文件公开/私有状态 |
| 获取预签名链接 | `GET /api/file/{id}/presign` | 获取文件的预签名下载链接 |
| 分享链接 | `GET /api/share/{id}` | 获取公开文件的分享 URL |

### 4. 图片处理

| 功能 | 说明 |
|------|------|
| 缩略图生成 | 通过 `/api/i/{id}?size=N` 动态生成 (N: 100-2000px)，自动缓存到 MinIO |
| 缩略图质量 | JPEG Q=85 |
| 缩略图缓存 | Redis 互斥锁防止并发生成 (300s TTL) |
| 水印添加 | `POST /api/file/{id}/watermark` - 自定义文字水印，可调位置和透明度 |
| 水印移除 | `DELETE /api/file/{id}/watermark` |
| 水印配置 | `GET /api/file/{id}/watermark` - 获取当前水印设置 |

### 5. 图床大厅（社区功能）

| 功能 | API | 说明 |
|------|-----|------|
| 发布到大厅 | `POST /api/hall/publish` | 选择已上传的公开图片，添加标题和标签发布 |
| 浏览大厅 | `GET /api/hall` | 分页浏览，支持 `sort` (latest/popular/views), `tag` 标签筛选 |
| 点赞/取消 | `POST /api/hall/{id}/like` | 切换点赞状态 |
| 删除帖子 | `DELETE /api/hall/{id}/delete` | 删除自己发布的帖子 |
| 标签列表 | `GET /api/hall/tags` | 获取所有标签及使用次数 |

### 6. 系统功能

| 功能 | API | 说明 |
|------|-----|------|
| 健康检查 | `GET /api/health` | 检查 MySQL/Redis/MinIO 连接状态 |
| 站点统计 | `GET /api/stats` | 总用户数、总文件数、总图片数、总存储量 |
| 运行指标 | `GET /api/metrics` | QPS、延迟百分位、缓存命中率、连接池状态 |
| 系统监控 | `/monitor/dashboard.html` | 独立监控面板 |
| 定时清理 | `POST /api/cleanup` | 清理孤儿分片和过期缓存 |

---

## 页面结构设计要求

### 整体布局

```
┌──────────┬──────────────────────────────────────┐
│          │  顶部栏 (搜索 + 主题切换)              │
│  左侧栏   ├──────────────────────────────────────┤
│  (290px)  │                                      │
│          │  主内容区                               │
│  Logo    │  (根据左侧栏选中的页面显示)              │
│  导航    │                                      │
│  存储条  │                                      │
│  用户卡  │                                      │
│          │                                      │
└──────────┴──────────────────────────────────────┘
```

### 左侧边栏

- Logo + 品牌名
- 导航菜单项:
  - 控制台 (Dashboard)
  - 我的图床 (My Library)
  - 图床大厅 (Gallery Hall)
- 存储空间卡片 (渐变背景 + 进度条)
- 用户信息卡 (头像 + 用户名 + 退出按钮)

### 页面 1: 控制台 (Dashboard)

- 欢迎条 (用户名 + 文件数/图片数/存储量统计卡片)
- 上传区域 (大面积拖拽区，支持拖拽上传和点击选择)
- 上传队列 (显示上传进度)

### 页面 2: 我的图床 (My Library)

- 搜索栏 (文件名搜索)
- 分类 Tab 栏 (全部/图片/文档/视频/音频，带数量角标)
- 视图切换 (网格视图 / 列表视图)
- 排序按钮 (按时间/大小/名称)
- 批量操作栏 (全选/已选数量/批量删除)
- 文件卡片 (图片显示缩略图，非图片显示类型图标 + 文件名/大小)
  - 底部操作栏: 链接 / MD / 公开私有 / 删除
- 列表视图 (图标 + 文件名 + 大小 + 时间 + 操作按钮)
- 分页组件

### 页面 3: 图床大厅 (Gallery Hall)

- 标题区 (标题 + 分类筛选按钮 + 发布按钮)
- 瀑布流图片墙 (3-4 列自适应)
  - 图片卡片: 图片 + 悬浮遮罩 (作者 + 标题 + 点赞/下载按钮)
- 发布弹窗: 选择图片 + 标题 + 标签

### 弹窗

- 文件详情弹窗: 预览 + 元数据 + 操作按钮
- 图片全屏预览: 原图查看
- 发布到大厅弹窗
- 登录/注册弹窗

---

## 设计风格要求

### 视觉风格

参考 Linear / Vercel / Notion 的现代企业级设计:

1. **Glass Morphism**: 半透明背景 + backdrop-filter 模糊
2. **渐变装饰**: 蓝紫渐变用于 Hero 区域和强调按钮
3. **大圆角**: 20px-40px 的圆角卡片
4. **柔和阴影**: 多层阴影营造层次感
5. **Aurora 背景**: 固定位置的模糊彩色圆形装饰

### 颜色系统

- 主色: #2563eb (蓝色)
- 强调: #7c3aed (紫色)
- 成功: #10b981 (绿色)
- 错误: #ef4444 (红色)
- 背景: #f4f7fb (亮色) / #0f172a (暗色)
- 卡片: rgba(255,255,255,0.72) + backdrop-filter: blur(18px)

### 字体

- Inter (英文) + PingFang SC / Microsoft YaHei (中文)
- 标题: font-weight: 800-900
- 正文: font-weight: 400-500

### 动画

- Hover 上浮: translateY(-4px) + shadow 变化
- 页面切换: 淡入动画
- 按钮 hover: scale(1.05) 微缩放
- 上传区域拖拽: scale(1.01) + 边框变色

### 响应式

- 桌面: 侧边栏 + 主内容
- 平板 (<1200px): 侧边栏折叠
- 手机 (<768px): 底部导航栏

---

## API 请求格式

### 认证

所有需要认证的 API 需要在 Header 中携带:
```
Authorization: Bearer <token>
```

### 通用响应格式

成功:
```json
{ "status": "success", ... }
```

错误:
```json
{ "error": "Error message" }
```

### 文件列表响应

```json
{
  "files": [
    {
      "file_id": "uuid",
      "filename": "photo.jpg",
      "size": 3240000,
      "mime_type": "image/jpeg",
      "width": 1920,
      "height": 1080,
      "upload_time": 1716900000,
      "is_public": true,
      "view_count": 42,
      "download_url": "http://host/api/i/uuid",
      "image_url": "http://host/api/i/uuid",
      "needs_preview": false
    }
  ],
  "total": 47,
  "type_counts": { "image": 10, "document": 36, "video": 0, "audio": 0 },
  "total_size": 53523386
}
```

### 图床大厅响应

```json
{
  "posts": [
    {
      "id": 1,
      "file_id": "uuid",
      "user_id": 1,
      "username": "admin",
      "title": "日落风景",
      "tags": "风景,自然",
      "likes": 42,
      "views": 128,
      "is_liked": false,
      "image_url": "http://host/api/i/uuid",
      "filename": "sunset.jpg",
      "file_size": 3240000,
      "created_at": 1716900000
    }
  ],
  "total": 100
}
```

---

## 交互细节

### 上传流程

1. 用户拖拽文件到上传区域 或 点击选择文件
2. 显示上传队列 (文件名 + 进度条 + 速度 + 剩余时间)
3. 小文件 (<5MB): 直接 POST 上传
4. 大文件 (≥5MB): 预签名 URL 直传 MinIO
5. 超大文件 (≥20MB): 分片上传
6. 上传成功: 自动刷新文件列表和存储统计
7. 上传失败: 显示错误原因，支持重试

### 文件操作

- 点击文件卡片: 打开详情弹窗 (预览 + 元数据)
- 复制链接: 一键复制分享链接到剪贴板
- 复制 Markdown: 复制 `![name](url)` 格式
- 公开/私有切换: 实时更新状态
- 删除: 二次确认后删除

### 图床大厅

- 点击图片: 全屏预览
- 点赞: 实时更新点赞数
- 筛选: 按分类/标签/排序筛选
- 发布: 选择已上传的公开图片 → 添加标题和标签 → 发布

---


