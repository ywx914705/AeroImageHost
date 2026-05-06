# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AeroImageHost is a high-performance image hosting system built in C++17 with a component-based monolithic architecture. It uses cpprestsdk (Casablanca) as the HTTP framework, MySQL 8.0 for metadata, MinIO (S3-compatible) for object storage, Redis for session management, and libvips for image processing. The frontend is a Vue 3 SPA served from `www/index.html` via CDN dependencies.

## Build Commands

```bash
# Local build (Ubuntu/Debian)
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run
./build/AeroImageHost config.json

# Docker deployment (all services: MySQL, Redis, MinIO, App)
# 需要先安装 docker-compose-plugin: sudo apt-get install -y docker-compose-plugin
# 国内用户需先配置 Docker 镜像加速，参见 README.md「配置国内镜像加速」
docker compose up -d
docker compose ps          # check status
docker compose logs -f app # follow app logs

# Rebuild Docker image after code changes
docker compose build app && docker compose up -d app

# Database initialization (local dev only, run in order)
mysql -u root -p < schema/01_init.sql
mysql -u root -p < schema/02_email_verification.sql
# schema/03_migrate.sql and schema/04_clean_data.sql are optional utilities
```

There are no automated tests or linters in this project. Changes are validated by building and manual testing.

## Architecture

The codebase follows a layered component architecture:

- **`main.cc`** — Entry point. Initialization order: Config → AsyncLog → ConnectionPool (MySQL, 32 conns) → RedisClient (16 conns) → MinIOClient → AeroQueue (4 worker threads) → HttpServer. On startup, runs `cleanupOrphanChunks()`, then spawns a background thread that repeats hourly. Registers SIGINT/SIGTERM for graceful shutdown.
- **`config/`** — JSON config loader (`Config` singleton, uses RapidJSON). Supports dot-notation paths like `"mysql.host"`.
- **`http/`** — HTTP layer built on cpprestsdk's `http_listener`.
  - `HttpServer` — Routes all API endpoints (`/api/*`). All handler methods are private members that parse requests and delegate to `Handlers.*`.
  - `Handlers.*` — Stateless business logic functions. Return `web::json::value` or `std::pair<vector<char>, string>`. Upload uses `std::async` to parallelize MySQL+MinIO writes. Batch delete parallelizes MinIO deletions. Email sending is async via AeroQueue.
  - `Auth` — Token-based auth. Tokens are 32-char random strings stored in Redis with 24h TTL. `Auth::verify()` extracts user from `Authorization: Bearer` header.
- **`storage/`** — Data access layer.
  - `FileMetaDAO` (singleton) — MySQL CRUD for file metadata. Uses `ConnectionPool` for all DB access.
  - `UsersDAO` (singleton) — User CRUD with prepared statements. Handles login, register, and email-based registration.
  - `MinIOClient` (singleton) — MinIO SDK wrapper. Handles put/get/delete/presign/compose (server-side multipart merge via ComposeObject).
  - `EmailVerificationDAO` — Email verification codes (time-limited).
- **`image/`** — `ImageProcessor` wraps libvips for on-demand thumbnail generation. Thumbnails are cached to MinIO `thumbs/` prefix with async writeback via AeroQueue.
- **`src/`** — Infrastructure.
  - `ConnectionPool` — MySQL connection pool (default 32). Uses 30s periodic validation with lazy re-validation on checkout.
  - `RedisClient` — Redis connection pool (default 16) via hiredis.
  - `AeroQueue` — Async task queue using `concurrentqueue.hpp` (lock-free MPMC queue) with 4 worker threads. Used for thumbnail cache writeback and async email sending.
  - `AsyncLog` — Async logger with dual-buffer flushing.
- **`utils/`** — URL encoding/decoding, UUID generation, MIME detection, `EmailSender` (SMTP with SSL/TLS), `PasswordHash` (PBKDF2-HMAC-SHA256, 100k iterations), `RateLimiter` (Redis sliding window, 5 attempts / 15 min per IP).
- **`www/`** — Static frontend. Vue 3 + Element Plus + Axios via CDN. Served directly by cpprestsdk.
- **`schema/`** — SQL DDL scripts: `01_init.sql` (users + files tables), `02_email_verification.sql` (verification codes), `03_migrate.sql` (add view_count column), `04_clean_data.sql` (utility truncate script).

## Key Gotchas

- **CMakeLists.txt uses GLOB_RECURSE for `.cc` files** but manually appends `storage/EmailVerificationDAO.cpp` and `utils/EmailSender.cpp`. When adding new source files, check whether they need manual appending.
- **miniocpp must be built from source** — it's not available via apt. See the Dockerfile for the build process (includes patches for CMake config and pugixml API compat).
- **Several libraries lack CMake config files** on Ubuntu: `curlpp`, `inih`, `nlohmann_json`, `pugixml`. The Dockerfile creates `.cmake` configs for them manually.
- **`config.json` is gitignored** — you must create it locally. Copy `config/config-docker.json` as a template and adjust hostnames to `localhost` for local dev.
- **Docker services use service names as hostnames** — in `config-docker.json`, MySQL host is `db`, Redis is `redis`, MinIO is `minio` (not `localhost`).
- **`minio.presign_endpoint`** — separate from `minio.endpoint`; used for generating presigned URLs that point to an externally accessible endpoint (e.g., a CDN or reverse-proxied MinIO). Falls back to `minio.endpoint` if empty.
- **`include/Handlers.hpp` was deleted** — handler declarations now live in `http/Handlers.hpp`.

## Key Patterns

- **Singletons everywhere**: `Config::instance()`, `ConnectionPool::getInstance()`, `RedisClient::instance()`, `MinIOClient::instance()`, `FileMetaDAO::instance()`, `UsersDAO::instance()`, `AeroQueue::instance()`.
- **Config-driven**: All settings come from `config.json` via `Config::instance().getString()/getInt()/getBool()` with dot-notation paths.
- **File IDs**: UUID strings used as primary keys in MinIO keys and database lookups (not auto-increment IDs).
- **Two upload modes**: Direct upload (small files, server proxies to MinIO) and multipart upload (large files, chunks uploaded through server proxy to MinIO, then merged via ComposeObject). The frontend auto-selects based on file size (5MB threshold).
- **Thumbnail with cache**: Generated on first request via `?w=200&h=200`, cached to MinIO `thumbs/` prefix. Subsequent requests read from cache. Cache writeback is async via AeroQueue.
- **Presigned URLs**: Generated lazily when opening file details (`/api/file/{id}/presign`), not during list queries.
- **Orphan chunk cleanup**: On startup and hourly thereafter, `cleanupOrphanChunks()` scans MinIO `chunks/` prefix and deletes any chunks older than 1 hour whose upload_id no longer exists in Redis.
- **Login rate limiting**: `RateLimiter` limits login attempts per IP to prevent brute-force attacks.

## API Endpoints

All routes are prefixed with `/api/`. Auth uses `Authorization: Bearer <token>` header.

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/auth/register` | POST | Register new user |
| `/api/auth/login` | POST | Login, returns token |
| `/api/auth/send-code` | POST | Send email verification code |
| `/api/auth/register/email` | POST | Register with email verification code |
| `/api/upload` | POST | Direct upload (small files) |
| `/api/upload/request` | POST | Request presigned upload URL (client-side upload) |
| `/api/upload/confirm` | POST | Confirm presigned upload completed |
| `/api/upload/multipart/init` | POST | Init multipart upload (large files) |
| `/api/upload/multipart/chunk` | POST | Upload chunk (server proxy) |
| `/api/upload/multipart/complete` | POST | Merge chunks |
| `/api/upload/multipart/cleanup` | POST | Clean up orphan chunks |
| `/api/files` | GET | List user's files (with `?offset=&limit=&search=`) |
| `/api/file/{id}` | GET/DELETE | Get metadata / delete file |
| `/api/file/{id}/presign` | GET | Generate presigned URL |
| `/api/file/{id}/public` | PUT | Toggle file public/private |
| `/api/i/{id}` | GET | Serve file (public, with optional `?w=&h=` thumbnail) |
| `/api/share/{id}` | GET | Get file share link (public endpoint) |
| `/api/batch/delete` | POST | Batch delete files |
| `/api/stats` | GET | Get system statistics |
| `/api/cleanup` | POST | Clean up orphan files (admin) |

Full API documentation with curl examples is in README.md.

## Configuration

`config.json` (gitignored, not committed) is required. A Docker-specific version exists at `config/config-docker.json`. Key sections: `http_port`, `max_file_size`, `mysql.*`, `redis.*`, `minio.*`, `smtp.*`, `log.*`, `security.*` (cors_origin, max_login_attempts, login_window_seconds), `cleanup.*` (multipart_ttl_seconds).

The `minio.public_url` must match the external URL where files are served (e.g., `http://localhost:8082/api/i/` for local dev, or your domain for production).

## Docker Routing

Nginx in Docker handles request routing (see `nginx.conf`):
- `/assets/` — Static file cache (long cache headers)
- `/images/` — MinIO passthrough
- `/api/` — Reverse proxy to the C++ app (port 8082 internal)
- `/minio/` — MinIO direct access
- `/` — Frontend SPA (`try_files` fallback to `index.html`)

## External Dependencies

System packages required (Ubuntu): `build-essential cmake pkg-config libssl-dev libcurl4-openssl-dev libmysqlclient-dev libvips-dev libcpprest-dev libhiredis-dev libcurlpp-dev libpugixml-dev libinih-dev`. Also requires: `miniocpp` (MinIO C++ SDK, built from source).

## Code Style

- Google C++ Style Guide
- Class names: CamelCase. Functions/variables: snake_case
- `.cc` files for implementation, `.hpp` for headers
- No tests exist — changes are validated by building and manual testing
