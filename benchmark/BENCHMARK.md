## ⚡ Performance Benchmark

### Test Environment

| Item | Configuration |
|------|--------------|
| **Framework** | C++ Drogon 1.8.0 (epoll + multi-reactor) |
| **Worker Threads** | 4 |
| **MySQL** | 8.0, connection pool=32 |
| **Redis** | 7.x, connection pool=16 (8 shards, lock-free) |
| **MinIO** | Single node, erasure code |
| **Benchmark Tool** | Custom C epoll-based HTTP client (keep-alive) |
| **Concurrency** | 50 persistent connections |
| **Duration** | 10 seconds per endpoint |
| **Build** | Release (-O2 -DNDEBUG) |

### Results Summary

| Endpoint | Description | QPS | P50 | P95 | P99 |
|----------|-------------|-----|-----|-----|-----|
| `GET /api/metrics` | Auth + atomic counters + 1s local cache | **81,562** | 0.53ms | 1.04ms | 4.00ms |
| `GET /api/stats` | Redis cached aggregate + in-process cache | **82,611** | 0.56ms | 0.89ms | 1.34ms |
| `GET /api/files` | Auth + Redis cache + in-process cache | **79,689** | 0.57ms | 0.96ms | 1.81ms |
| `GET /api/health` | MySQL + Redis + MinIO deep check | **12,889** | 3.49ms | 6.66ms | 9.59ms |
| `POST /api/auth/login` | PBKDF2 (100k iterations) | **67** | 218.6ms | 716.0ms | 922.7ms |
| `POST /api/upload` | MinIO object storage write | **101** | 13.1ms | 160.1ms | 160.1ms |

> **Note**: Login QPS is intentionally limited by PBKDF2 key derivation (100,000 iterations), which is a security best practice against brute-force attacks. Upload QPS is bounded by MinIO disk I/O latency.

### Architecture Highlights

- **Redis Sharded Connection Pool**: 8 independent shards eliminate global mutex contention, enabling linear scaling under high concurrency
- **Sharded LRU Token Cache**: 16-shard read-write lock design eliminates Auth verification bottleneck, supporting 80K+ QPS on authenticated endpoints
- **Multi-layer Cache**: Redis Hash cache for file metadata with null-value caching (anti-penetration), index-based batch invalidation, plus in-process 1s cache for hot endpoints
- **Metrics Local Cache**: 1-second atomic timestamp cache avoids JSON rebuild on every request, boosting metrics endpoint from 12K to 83K QPS
- **Stats In-Process Cache**: Atomic timestamp + shared_ptr atomic store pattern enables lock-free reads for cached stats, reaching 88K QPS
- **Async Task Queue**: MinIO I/O operations offloaded from Drogon reactor threads via `moodycamel::ConcurrentQueue` to prevent event loop blocking
- **Pipeline View Sync**: Background view count synchronization uses Redis Pipeline with write-after-DB-confirm pattern to prevent data loss
- **Atomic Rate Limiting**: Lua script combines check + increment into a single Redis roundtrip
- **Dynamic Connection Pool**: MySQL pool scales under load, auto-shrinks when idle

### How to Run Benchmark

```bash
cd benchmark
chmod +x run.sh
./run.sh
```

This will compile the C benchmark tool and execute the full benchmark suite. Results are saved to `benchmark/BENCHMARK.md`.
