#!/bin/sh

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
monitor=${HEALTH_MONITOR_MOCK:-"$project_dir/.build/health-monitor-mock"}
if [ ! -x "$monitor" ]; then
    echo "Build the mock server first with: make integration-build" >&2
    exit 1
fi
cd "$project_dir"

temporary_dir=$(mktemp -d)
monitor_pid=
input_open=false

cleanup()
{
    if [ -n "$monitor_pid" ] && kill -0 "$monitor_pid" 2>/dev/null; then
        kill -TERM "$monitor_pid" 2>/dev/null || true
        wait "$monitor_pid" 2>/dev/null || true
    fi
    if [ "$input_open" = true ]; then
        exec 3>&-
    fi
    rm -rf "$temporary_dir"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM

mkdir "$temporary_dir/bin"
cat >"$temporary_dir/bin/stress-ng" <<'EOF'
#!/bin/sh
echo "FAKE stress-ng: $*"
EOF
cat >"$temporary_dir/bin/sleep" <<'EOF'
#!/bin/sh
exec /bin/sleep 0.8
EOF
chmod +x "$temporary_dir/bin/stress-ng" "$temporary_dir/bin/sleep"

input_fifo="$temporary_dir/input"
output_file="$temporary_dir/output"
mkfifo "$input_fifo"
exec 3<>"$input_fifo"
input_open=true

# The PID-based port keeps repeated local runs unlikely to collide.
port=$((20000 + ($$ % 40000)))
PATH="$temporary_dir/bin:$PATH" env -u DISPLAY -u WAYLAND_DISPLAY \
    "$monitor" --host 127.0.0.1 --port "$port" --interval 5 100 \
    --cpu-threshold 100 <"$input_fifo" >"$output_file" 2>&1 &
monitor_pid=$!

wait_for_text()
{
    text=$1
    attempts=$2
    while [ "$attempts" -gt 0 ]; do
        if grep -Fq "$text" "$output_file"; then
            return 0
        fi
        if ! kill -0 "$monitor_pid" 2>/dev/null; then
            cat "$output_file" >&2
            echo "Mock monitor exited before printing: $text" >&2
            return 1
        fi
        /bin/sleep 0.1
        attempts=$((attempts - 1))
    done
    cat "$output_file" >&2
    echo "Timed out waiting for mock monitor output: $text" >&2
    return 1
}

wait_for_occurrences()
{
    text=$1
    expected=$2
    attempts=$3
    while [ "$(grep -Fc "$text" "$output_file" || true)" -lt "$expected" ] &&
          [ "$attempts" -gt 0 ]; do
        /bin/sleep 0.1
        attempts=$((attempts - 1))
    done
    if [ "$(grep -Fc "$text" "$output_file" || true)" -lt "$expected" ]; then
        cat "$output_file" >&2
        echo "Timed out waiting for $expected occurrences of: $text" >&2
        return 1
    fi
}

count_low_samples()
{
    grep -c '^MOCK GPIO 17: LOW$' "$output_file" || true
}

wait_for_text "Web server started" 50
attempts=30
while [ "$(count_low_samples)" -lt 2 ] && [ "$attempts" -gt 0 ]; do
    /bin/sleep 0.1
    attempts=$((attempts - 1))
done
if [ "$(count_low_samples)" -ne 2 ]; then
    cat "$output_file" >&2
    echo "Expected GPIO initialization and one initial sample." >&2
    exit 1
fi

# Poll slices must not trigger samples, and the next sample must not arrive
# materially earlier than the configured five-second interval.
/bin/sleep 4.2
if [ "$(count_low_samples)" -ne 2 ]; then
    cat "$output_file" >&2
    echo "A new sample occurred before the configured five-second interval." >&2
    exit 1
fi
echo "PASS: no early sample during the first 4.2 seconds of a five-second interval."

# Exercise help while a harmless, deliberately slow fake test runs.
printf 'h\nt\n1\n' >&3
wait_for_text "Status  : Running" 30
wait_for_text "Show commands and test-menu help" 20
echo "PASS: keyboard help remains responsive while a test child runs."

# The fake test takes over five seconds, so this sample must occur during it.
attempts=45
while [ "$(count_low_samples)" -lt 3 ] && [ "$attempts" -gt 0 ]; do
    /bin/sleep 0.1
    attempts=$((attempts - 1))
done
if [ "$(count_low_samples)" -lt 3 ] ||
   grep -Fq "Test 1 (CPU load escalation) finished successfully." "$output_file"; then
    cat "$output_file" >&2
    echo "Sampling did not continue while the fake test child was active." >&2
    exit 1
fi
echo "PASS: a scheduled sample occurred while the test child was active."

wait_for_text "Test 1 (CPU load escalation) finished successfully." 70

# Start another fake run and quit before it finishes to cover orderly child
# process-group shutdown as well as natural child completion.
printf 't\n1\n' >&3
wait_for_occurrences "Launching Test 1: CPU load escalation" 2 30
wait_for_occurrences "Status  : Running" 2 20
printf 'q\n' >&3
exec 3>&-
input_open=false
if ! wait "$monitor_pid"; then
    cat "$output_file" >&2
    echo "Mock monitor did not shut down cleanly." >&2
    exit 1
fi
monitor_pid=
grep -Fq "Shutdown requested while Test 1 (CPU load escalation) is running." "$output_file"
grep -Fq "Test 1 (CPU load escalation) stopped by signal" "$output_file"
grep -Fq "Monitoring stopped; warning LED is off." "$output_file"
echo "PASS: child completion and q shutdown of an active child were reported."
