#!/usr/bin/env bash
# Builds minidrc, generates layouts of increasing size and prints a Markdown
# table comparing the spacing algorithms. Usage: scripts/run_benchmarks.sh [threads]
set -euo pipefail
cd "$(dirname "$0")/.."
THREADS=${1:-$(nproc)}
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build -j > /dev/null
mkdir -p build/layouts

echo "CPU: $(lscpu | sed -n 's/^Model name:[[:space:]]*//p'), threads: ${THREADS}"
echo
echo "| shapes | violations | naive all-pairs | sweep line | grid, 1 thread | grid, ${THREADS} threads | best speedup |"
echo "|---:|---:|---:|---:|---:|---:|---:|"
for n in 10000 100000 1000000 4000000; do
  f=build/layouts/layout_$n.txt
  [[ -f $f ]] || ./build/minidrc gen --rects $n --seed 1 --out "$f" 2> /dev/null
  ./build/minidrc bench "$f" examples/rules.txt --threads "$THREADS" --markdown
done
echo
echo "Spacing-check time only. Every row is verified: all algorithms must report identical violations."
