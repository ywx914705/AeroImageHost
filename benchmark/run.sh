#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== AeroImageHost Benchmark Build ==="

cd "$SCRIPT_DIR"

if [ ! -f bench_c ] || [ bench.c -nt bench_c ]; then
    echo "Compiling bench_c ..."
    gcc -O2 -o bench_c bench.c -lm
    echo "Build OK"
else
    echo "bench_c is up to date"
fi

echo ""
echo "Running benchmark ..."
python3 run_bench.py

echo ""
echo "Done!"
