#!/bin/sh

set -eu

if ! command -v stress-ng >/dev/null 2>&1; then
    echo "stress-ng is required for Test 2. Install it with: sudo apt install stress-ng" >&2
    exit 127
fi

if [ ! -r /proc/meminfo ]; then
    echo "Cannot safely run Test 2: /proc/meminfo is not readable." >&2
    exit 1
fi

echo "Test 2: RAM allocation escalation (one worker, 10 seconds per stage)"
echo "Each stage requires enough MemAvailable to allocate the stage and retain a 1024 MiB reserve."
started=0
for mb in 256 512 768 1024; do
    available_kb=$(awk '$1 == "MemAvailable:" { print $2; found = 1; exit } END { if (!found) exit 1 }' /proc/meminfo) || {
        echo "Cannot safely run Test 2: MemAvailable could not be read." >&2
        exit 1
    }
    case $available_kb in
        ''|*[!0-9]*)
            echo "Cannot safely run Test 2: MemAvailable was not a valid number." >&2
            exit 1
            ;;
    esac

    required_kb=$(((mb + 1024) * 1024))
    if [ "$available_kb" -lt "$required_kb" ]; then
        echo "Skipping ${mb} MB stage: ${available_kb} kB available; ${required_kb} kB required to retain the 1024 MiB reserve."
        continue
    fi

    echo "Testing with ${mb} MB (MemAvailable: $((available_kb / 1024)) MiB; retaining at least 1024 MiB)..."
    started=1
    stress-ng --vm 1 --vm-bytes "${mb}M" --vm-keep --timeout 10s
    sleep 2
done

if [ "$started" -eq 0 ]; then
    echo "No RAM stages ran because the safety reserve was unavailable."
fi
