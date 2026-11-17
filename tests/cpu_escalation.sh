#!/bin/sh

set -eu

if ! command -v stress-ng >/dev/null 2>&1; then
    echo "stress-ng is required for Test 1. Install it with: sudo apt install stress-ng" >&2
    exit 127
fi

echo "Test 1: CPU load escalation (4 workers, 10 seconds per stage)"
for load in 20 40 50 60 70 80 90; do
    echo "Testing all 4 CPU cores at ${load}%..."
    stress-ng --cpu 4 --cpu-load "$load" --timeout 10s
    sleep 2
done
