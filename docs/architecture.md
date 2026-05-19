# AeroImageHost 系统架构图

> 基于源码真实结构绘制，所有组件、类名、路由均与代码一致

---

## 1. 系统分层架构图

```mermaid
%%{init: {'theme': 'base', 'themeVariables': { 'fontSize': '16px'}}}%%
graph LR
    Client([fa:fa-user 客户端])
    Nginx{{fa:fa-server Nginx}}
    Drogon[/fa:fa-bolt Drogon/]
    Filters[fa:fa-filter 过滤器层]
    Controllers[fa:fa-gamepad 控制器层]
    Handlers[fa:fa-cogs 业务逻辑层]
    DAO[fa:fa-database DAO层]
    Pools[fa:fa-link 连接池]
    Storage[(fa:fa-hdd 存储)]
    Infra[fa:fa-wrench 基础设施]

    Client -->|HTTP| Nginx
    Nginx -->|反向代理| Drogon
    Drogon --> Filters
    Filters --> Controllers
    Controllers --> Handlers
    Handlers --> DAO
    Handlers --> Infra
    DAO --> Pools
    Pools --> Storage

    style Client fill:#e3f2fd,stroke:#1976d2,stroke-width:2px
    style Nginx fill:#fff3e0,stroke:#f57c00,stroke-width:2px
    style Drogon fill:#e8f5e9,stroke:#388e3c,stroke-width:3px
    style Filters fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px
    style Controllers fill:#fce4ec,stroke:#c2185b,stroke-width:2px
    style Handlers fill:#e0f2f1,stroke:#00796b,stroke-width:2px
    style DAO fill:#fff8e1,stroke:#ffa000,stroke-width:2px
    style Pools fill:#e8eaf6,stroke:#303f9f,stroke-width:2px
    style Storage fill:#e1f5fe,stroke:#0288d1,stroke-width:2px
    style Infra fill:#f5f5f5,stroke:#616161,stroke-width:1px,stroke-dasharray: 5 5
```

---

## 2. 控制器与路由图

```mermaid
%%{init: {'theme': 'base', 'themeVariables': { 'fontSize': '15px'}}}%%
graph TB
    Drogon[/fa:fa-bolt Drogon HTTP Server/]
    
    AuthFilter[fa:fa-filter AuthFilter<br/>JWT Token 鉴权]
    
    Auth[fa:fa-key AuthController]
    Upload[fa:fa-cloud-upload UploadController]
    File[fa:fa-folder FileController]
    System[fa:fa-heartbeat SystemController]
    
    Drogon --> AuthFilter
    AuthFilter --> Auth
    AuthFilter --> Upload
    AuthFilter --> File
    AuthFilter --> System
    
    style Drogon fill:#e8f5e9,stroke:#388e3c,stroke-width:3px
    style AuthFilter fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px
    style Auth fill:#fce4ec,stroke:#c2185b,stroke-width:2px
    style Upload fill:#fce4ec,stroke:#c2185b,stroke-width:2px
    style File fill:#fce4ec,stroke:#c2185b,stroke-width:2px
    style System fill:#fce4ec,stroke:#c2185b,stroke-width:2px
```

### 路由清单

| 控制器 | 路由 | 方法 | 说明 |
|--------|------|------|------|
| **AuthController** | /api/auth/register | POST | 用户注册 |
| | /api/auth/login | POST | 用户登录 |
| | /api/auth/send-code | POST | 发送邮箱验证码 |
| | /api/auth/email-register | POST | 邮箱验证码注册 |
| **UploadController** | /api/upload | POST | 直接上传文件 |
| | /api/upload/presign | POST | 申请预签名上传 URL |
| | /api/upload/confirm | POST | 确认预签名上传完成 |
| | /api/upload/multipart/init | POST | 初始化分片上传 |
| | /api/upload/multipart/chunk | POST | 上传单个分片 |
| | /api/upload/multipart/complete | POST | 完成分片上传 |
| | /api/upload/multipart/cleanup | POST | 清理分片上传临时数据 |
| **FileController** | /api/files | GET | 获取文件列表 |
| | /api/file/{id} | DELETE | 删除指定文件 |
| | /api/files/batch-delete | POST | 批量删除文件 |
| | /api/file/{id}/public | PUT | 切换公开/私有状态 |
| | /api/file/{id}/presign | GET | 获取预签名下载 URL |
| | /api/share/{id} | GET | 获取文件分享信息 |
| **SystemController** | /api/health | GET | 深度健康检查 |
| | /api/stats | GET | 系统统计信息 |
| | /api/metrics | GET | 性能指标 JSON |
| | /api/cleanup | POST | 触发孤儿分片清理 |

---

## 3. 数据访问层架构

```mermaid
%%{init: {'theme': 'base', 'themeVariables': { 'fontSize': '15px'}}}%%
graph TB
    Handlers[fa:fa-cogs Handlers<br/>业务逻辑]
    
    UsersDAO[fa:fa-users UsersDAO]
    FileMetaDAO[fa:fa-file FileMetaDAO]
    EmailDAO[fa:fa-envelope EmailVerificationDAO]
    
    ConnPool[fa:fa-link ConnectionPool<br/>MySQL 连接池]
    RedisClient[fa:fa-bolt RedisClient<br/>Redis 连接池]
    MinIOClient[fa:fa-hdd MinIOClient<br/>S3 API 客户端]
    
    MySQL[(fa:fa-database MySQL 8.0)]
    Redis[(fa:fa-bolt Redis 7.x)]
    MinIO[(fa:fa-cloud MinIO S3)]
    
    Handlers --> UsersDAO
    Handlers --> FileMetaDAO
    Handlers --> EmailDAO
    Handlers --> RedisClient
    Handlers --> MinIOClient
    
    UsersDAO --> ConnPool
    FileMetaDAO --> ConnPool
    EmailDAO --> ConnPool
    
    ConnPool --> MySQL
    RedisClient --> Redis
    MinIOClient --> MinIO
    
    style Handlers fill:#e0f2f1,stroke:#00796b,stroke-width:2px
    style UsersDAO fill:#fff8e1,stroke:#ffa000,stroke-width:2px
    style FileMetaDAO fill:#fff8e1,stroke:#ffa000,stroke-width:2px
    style EmailDAO fill:#fff8e1,stroke:#ffa000,stroke-width:2px
    style ConnPool fill:#e8eaf6,stroke:#303f9f,stroke-width:2px
    style RedisClient fill:#e8eaf6,stroke:#303f9f,stroke-width:2px
    style MinIOClient fill:#e8eaf6,stroke:#303f9f,stroke-width:2px
    style MySQL fill:#e1f5fe,stroke:#0288d1,stroke-width:2px
    style Redis fill:#ffebee,stroke:#c62828,stroke-width:2px
    style MinIO fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px
```

---

## 4. 基础设施组件

```mermaid
%%{init: {'theme': 'base', 'themeVariables': { 'fontSize': '15px'}}}%%
graph LR
    Handlers[fa:fa-cogs Handlers]
    
    ImageProcessor[fa:fa-image ImageProcessor<br/>libvips 缩略图]
    EmailSender[fa:fa-envelope-o EmailSender<br/>SMTP 邮件]
    RateLimiter[fa:fa-shield RateLimiter<br/>Redis 限流]
    PasswordHash[fa:fa-lock PasswordHash<br/>PBKDF2 加密]
    
    AeroQueue[fa:fa-tasks AeroQueue<br/>异步任务队列]
    Metrics[fa:fa-line-chart MetricsCollector<br/>性能指标]
    Log[fa:fa-file-text-o Log<br/>日志系统]
    
    Handlers --> ImageProcessor
    Handlers --> EmailSender
    Handlers --> RateLimiter
    Handlers --> PasswordHash
    Handlers -.-> AeroQueue
    Handlers -.-> Metrics
    Handlers -.-> Log
    
    style Handlers fill:#e0f2f1,stroke:#00796b,stroke-width:2px
    style ImageProcessor fill:#f5f5f5,stroke:#616161,stroke-width:1px
    style EmailSender fill:#f5f5f5,stroke:#616161,stroke-width:1px
    style RateLimiter fill:#f5f5f5,stroke:#616161,stroke-width:1px
    style PasswordHash fill:#f5f5f5,stroke:#616161,stroke-width:1px
    style AeroQueue fill:#f5f5f5,stroke:#616161,stroke-width:1px,stroke-dasharray: 5 5
    style Metrics fill:#f5f5f5,stroke:#616161,stroke-width:1px,stroke-dasharray: 5 5
    style Log fill:#f5f5f5,stroke:#616161,stroke-width:1px,stroke-dasharray: 5 5
```

---

## 5. 文件上传流程

```mermaid
%%{init: {'theme': 'base', 'themeVariables': { 'fontSize': '14px'}}}%%
sequenceDiagram
    autonumber
    actor C as 客户端
    participant N as Nginx
    participant D as Drogon
    participant F as AuthFilter
    participant U as UploadController
    participant H as Handlers
    participant M as MinIOClient
    participant R as RedisClient
    participant My as MySQL

    C->>N: POST /api/upload + 文件
    N->>D: 转发请求
    D->>F: doFilter()
    F->>R: 验证 Token
    R-->>F: user_id
    F-->>D: 通过
    D->>U: upload()
    U->>H: handleUpload()
    H->>H: 生成 UUID
    H->>M: putObject()
    M-->>H: 成功
    H->>My: INSERT files
    My-->>H: 成功
    H->>R: 清除缓存
    H-->>U: file_id
    U-->>D: JSON
    D-->>N: 200 OK
    N-->>C: 上传结果
```

---

## 6. 文件访问流程

```mermaid
%%{init: {'theme': 'base', 'themeVariables': { 'fontSize': '14px'}}}%%
sequenceDiagram
    autonumber
    actor C as 客户端
    participant N as Nginx
    participant D as Drogon
    participant F as FileController
    participant H as Handlers
    participant My as MySQL
    participant M as MinIOClient
    participant I as ImageProcessor
    participant R as RedisClient

    C->>N: GET /api/i/{id}?w=200
    N->>D: 转发
    D->>F: 路由匹配
    F->>H: handleGetFile()
    H->>My: SELECT 元数据
    My-->>H: FileMeta
    
    alt 需要缩略图
        H->>R: 查缓存
        alt 命中
            R-->>H: 返回数据
        else 未命中
            H->>M: getObject()
            M-->>H: 原图
            H->>I: generateThumbnail()
            I-->>H: 缩略图
        end
    else 原图
        H->>M: presignGetUrl()
        M-->>H: 预签名URL
    end
    
    H-->>F: 响应数据
    F-->>D: HTTP响应
    D-->>N: 文件流
    N-->>C: 图片内容
```

---

## 7. 用户认证流程

```mermaid
%%{init: {'theme': 'base', 'themeVariables': { 'fontSize': '14px'}}}%%
sequenceDiagram
    autonumber
    actor C as 客户端
    participant N as Nginx
    participant D as Drogon
    participant A as AuthController
    participant H as Handlers
    participant U as UsersDAO
    participant P as PasswordHash
    participant R as RedisClient
    participant My as MySQL

    C->>N: POST /api/auth/login
    N->>D: 转发
    D->>A: login()
    A->>H: handleLogin()
    H->>U: loginUser()
    U->>My: SELECT 用户
    My-->>U: user_id, hash
    U-->>H: 返回数据
    
    alt 用户存在
        H->>P: verify()
        P-->>H: 结果
        alt 密码正确
            H->>R: SET Token
            H-->>A: 成功
        else 密码错误
            H->>R: 记录失败
            H-->>A: 401
        end
    else 用户不存在
        H->>R: 记录失败
        H-->>A: 401
    end
    
    A-->>D: JSON
    D-->>N: 响应
    N-->>C: 结果
```

---

## 8. 部署架构

```mermaid
%%{init: {'theme': 'base', 'themeVariables': { 'fontSize': '15px'}}}%%
graph TB
    Client[fa:fa-user 客户端]
    Nginx{{fa:fa-server Nginx<br/>80/443}}
    App[/fa:fa-bolt AeroImageHost<br/>8082/]
    MySQL[(fa:fa-database MySQL<br/>3306)]
    Redis[(fa:fa-bolt Redis<br/>6379)]
    MinIO[(fa:fa-cloud MinIO<br/>9000)]
    
    Client -->|HTTP| Nginx
    Nginx -->|/api| App
    Nginx -->|静态资源| www
    App -->|SQL| MySQL
    App -->|Redis| Redis
    App -->|S3| MinIO
    
    style Client fill:#e3f2fd,stroke:#1976d2
    style Nginx fill:#fff3e0,stroke:#f57c00
    style App fill:#e8f5e9,stroke:#388e3c,stroke-width:3px
    style MySQL fill:#e1f5fe,stroke:#0288d1
    style Redis fill:#ffebee,stroke:#c62828
    style MinIO fill:#e8f5e9,stroke:#2e7d32
```

---

## 9. 技术栈

```mermaid
%%{init: {'theme': 'base', 'themeVariables': { 'fontSize': '15px'}}}%%
graph LR
    Frontend[fa:fa-desktop 前端<br/>Vue 3 + Element Plus]
    Backend[fa:fa-server 后端<br/>C++17 + Drogon]
    Database[fa:fa-database 数据<br/>MySQL + Redis + MinIO]
    DevOps[fa:fa-cogs 运维<br/>Docker + Nginx + CI/CD]
    
    Frontend -->|Axios| Backend
    Backend -->|SQL| Database
    Backend -->|Redis| Database
    Backend -->|S3| Database
    DevOps -->|部署| Backend
    DevOps -->|部署| Database
    
    style Frontend fill:#e3f2fd,stroke:#1976d2
    style Backend fill:#e8f5e9,stroke:#388e3c,stroke-width:3px
    style Database fill:#fff8e1,stroke:#ffa000
    style DevOps fill:#f3e5f5,stroke:#7b1fa2
```
