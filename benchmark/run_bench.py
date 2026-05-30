#!/usr/bin/env python3
import subprocess, json, time, sys, os, urllib.request

HOST = "127.0.0.1"
PORT = 8082
CONNS = 200
DURATION = 10
BENCH_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bench_c")

def ensure_server():
    try:
        urllib.request.urlopen(f"http://{HOST}:{PORT}/api/health", timeout=3)
        return True
    except:
        return False

def get_token():
    try:
        req = urllib.request.Request(
            f"http://{HOST}:{PORT}/api/auth/login",
            data=json.dumps({"account": "914718474", "password": "123456"}).encode(),
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())["token"]
    except Exception as e:
        print(f"Login failed: {e}")
        return None

def run_bench(name, path, extra_header=""):
    cmd = [BENCH_BIN, HOST, str(PORT), path, str(CONNS), str(DURATION)]
    if extra_header:
        cmd.append(extra_header)
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=DURATION + 30)
    data = {}
    for line in result.stdout.strip().split("\n"):
        if ":" in line:
            k, v = line.split(":", 1)
            data[k.strip()] = v.strip()
    return {
        "name": name,
        "path": path,
        "qps": int(float(data.get("QPS", 0))),
        "p50": float(data.get("LATENCY_P50", 0)),
        "p90": float(data.get("LATENCY_P90", 0)),
        "p95": float(data.get("LATENCY_P95", 0)),
        "p99": float(data.get("LATENCY_P99", 0)),
        "avg": float(data.get("LATENCY_AVG", 0)),
        "max": float(data.get("LATENCY_MAX", 0)),
        "requests": int(data.get("REQUESTS", 0)),
        "conns": CONNS,
        "duration": DURATION,
        "2xx": int(data.get("2xx", 0)),
        "4xx": int(data.get("4xx", 0)),
        "5xx": int(data.get("5xx", 0)),
        "errors": int(data.get("ERRORS", 0)),
    }

def run_login_bench():
    results = []
    count = 100
    workers = 20
    import concurrent.futures
    def do_login(i):
        start = time.time()
        try:
            req = urllib.request.Request(
                f"http://{HOST}:{PORT}/api/auth/login",
                data=json.dumps({"account": "914718474", "password": "123456"}).encode(),
                headers={"Content-Type": "application/json"},
                method="POST"
            )
            with urllib.request.urlopen(req, timeout=10) as resp:
                code = resp.status
            elapsed = (time.time() - start) * 1000
            return (code, elapsed)
        except:
            return (0, (time.time() - start) * 1000)

    start = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futs = [pool.submit(do_login, i) for i in range(count)]
        for f in concurrent.futures.as_completed(futs):
            results.append(f.result())
    total_time = time.time() - start
    latencies = sorted([r[1] for r in results])
    success = sum(1 for r in results if 200 <= r[0] < 300)
    return {
        "name": "Login (PBKDF2)",
        "path": "POST /api/auth/login",
        "qps": int(count / total_time),
        "p50": latencies[int(len(latencies)*0.5)],
        "p90": latencies[int(len(latencies)*0.9)],
        "p95": latencies[int(len(latencies)*0.95)],
        "p99": latencies[int(len(latencies)*0.99)],
        "avg": sum(latencies) / len(latencies),
        "max": latencies[-1],
        "requests": count,
        "conns": workers,
        "duration": round(total_time, 1),
        "2xx": success,
        "4xx": 0,
        "5xx": 0,
        "errors": count - success,
    }

def run_upload_bench(token):
    results = []
    count = 20
    import concurrent.futures
    def do_upload(i):
        start = time.time()
        try:
            cmd = [
                "curl", "-s", "-o", "/dev/null", "-w", "%{http_code}",
                "-X", "POST", f"http://{HOST}:{PORT}/api/upload",
                "-H", f"Authorization: Bearer {token}",
                "-F", "file=@/tmp/test_image.png"
            ]
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            code = int(r.stdout.strip()) if r.stdout.strip().isdigit() else 0
            elapsed = (time.time() - start) * 1000
            return (code, elapsed)
        except:
            return (0, (time.time() - start) * 1000)

    start = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        futs = [pool.submit(do_upload, i) for i in range(count)]
        for f in concurrent.futures.as_completed(futs):
            results.append(f.result())
    total_time = time.time() - start
    latencies = sorted([r[1] for r in results])
    success = sum(1 for r in results if 200 <= r[0] < 300)
    return {
        "name": "Upload (MinIO)",
        "path": "POST /api/upload",
        "qps": int(count / total_time),
        "p50": latencies[int(len(latencies)*0.5)],
        "p90": latencies[int(len(latencies)*0.9)],
        "p95": latencies[int(len(latencies)*0.95)],
        "p99": latencies[int(len(latencies)*0.99)],
        "avg": sum(latencies) / len(latencies),
        "max": latencies[-1],
        "requests": count,
        "conns": 5,
        "duration": round(total_time, 1),
        "2xx": success,
        "4xx": 0,
        "5xx": 0,
        "errors": count - success,
    }

def generate_report(all_results):
    lines = []
    lines.append("## Performance Benchmark")
    lines.append("")
    lines.append("Test environment:")
    lines.append("")
    lines.append("- **Server**: C++ Drogon framework, 16 worker threads")
    lines.append("- **Database**: MySQL 8.0 (pool=32), Redis 7 (pool=16, 8 shards), MinIO")
    lines.append("- **Client**: Custom C epoll-based HTTP benchmark tool")
    lines.append(f"- **Concurrency**: {CONNS} persistent connections")
    lines.append(f"- **Duration**: {DURATION}s per endpoint")
    lines.append("- **Keep-Alive**: Enabled (HTTP/1.1)")
    lines.append("")
    lines.append("### Results Summary")
    lines.append("")
    lines.append("| Endpoint | QPS | P50 | P95 | P99 | Avg | Max |")
    lines.append("|----------|-----|-----|-----|-----|-----|-----|")
    for r in all_results:
        lines.append(f"| {r['name']} | **{r['qps']:,}** | {r['p50']:.1f}ms | {r['p95']:.1f}ms | {r['p99']:.1f}ms | {r['avg']:.1f}ms | {r['max']:.1f}ms |")
    lines.append("")
    lines.append("### Detailed Results")
    lines.append("")
    for r in all_results:
        lines.append(f"#### {r['name']}")
        lines.append("")
        lines.append(f"- **Path**: `{r['path']}`")
        lines.append(f"- **QPS**: {r['qps']:,} req/s")
        lines.append(f"- **Total Requests**: {r['requests']:,} in {r['duration']}s")
        lines.append(f"- **Concurrency**: {r['conns']} connections")
        lines.append(f"- **Latency**: avg={r['avg']:.2f}ms, p50={r['p50']:.2f}ms, p90={r['p90']:.2f}ms, p95={r['p95']:.2f}ms, p99={r['p99']:.2f}ms, max={r['max']:.2f}ms")
        lines.append(f"- **Status Codes**: 2xx={r['2xx']}, 4xx={r['4xx']}, 5xx={r['5xx']}, errors={r['errors']}")
        lines.append("")
    return "\n".join(lines)

if __name__ == "__main__":
    print("AeroImageHost Performance Benchmark")
    print("=" * 50)

    if not ensure_server():
        print("ERROR: Server is not running on {HOST}:{PORT}")
        sys.exit(1)
    print(f"Server is running on {HOST}:{PORT}")

    token = get_token()
    if not token:
        print("ERROR: Cannot get auth token")
        sys.exit(1)
    print(f"Auth token obtained")

    all_results = []

    print(f"\n[1/6] Benchmarking /api/health ...")
    r = run_bench("Health Check", "/api/health")
    all_results.append(r)
    print(f"  QPS: {r['qps']:,}  P50: {r['p50']:.1f}ms  P99: {r['p99']:.1f}ms")

    print(f"[2/6] Benchmarking /api/stats ...")
    r = run_bench("Stats (Cached)", "/api/stats")
    all_results.append(r)
    print(f"  QPS: {r['qps']:,}  P50: {r['p50']:.1f}ms  P99: {r['p99']:.1f}ms")

    print(f"[3/6] Benchmarking /api/files ...")
    r = run_bench("File List (Auth+Cache)", "/api/files", f"Authorization: Bearer {token}")
    all_results.append(r)
    print(f"  QPS: {r['qps']:,}  P50: {r['p50']:.1f}ms  P99: {r['p99']:.1f}ms")

    print(f"[4/6] Benchmarking /api/metrics ...")
    r = run_bench("Metrics (Auth)", "/api/metrics", f"Authorization: Bearer {token}")
    all_results.append(r)
    print(f"  QPS: {r['qps']:,}  P50: {r['p50']:.1f}ms  P99: {r['p99']:.1f}ms")

    print(f"[5/6] Benchmarking Login (Python HTTP client) ...")
    r = run_login_bench()
    all_results.append(r)
    print(f"  QPS: {r['qps']:,}  P50: {r['p50']:.1f}ms  P99: {r['p99']:.1f}ms")

    print(f"[6/6] Benchmarking Upload (curl subprocess) ...")
    r = run_upload_bench(token)
    all_results.append(r)
    print(f"  QPS: {r['qps']:,}  P50: {r['p50']:.1f}ms  P99: {r['p99']:.1f}ms")

    report = generate_report(all_results)

    report_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "BENCHMARK.md")
    with open(report_path, "w") as f:
        f.write(report + "\n")

    print(f"\n{'=' * 50}")
    print(f"Report saved to: {report_path}")
    print(f"\n{report}")
