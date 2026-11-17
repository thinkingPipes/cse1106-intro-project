# Hardware demonstration test cases

These procedures demonstrate the Raspberry Pi equipment health monitor's CPU,
memory, and root filesystem readings and its GPIO 17 warning LED. They assume a
Raspberry Pi with 4 GB RAM, 64 GB storage, four CPU cores, and the LED wired to
BCM GPIO 17 through a current-limiting resistor.

Install build dependencies as described in the project README. Install
`stress-ng` with `sudo apt install stress-ng` before tests 1 or 2. Start the
monitor from the project directory so the test scripts and `web/` assets can be
found. For tests that should exercise the warning LED, run the
production binary with GPIO access:

```sh
cd /path/to/cse1106-intro-project
make
./.build/health-monitor --host 0.0.0.0 --port 8080
```

Open the printed dashboard URL on the Pi. From another device, replace
`<PI-LAN-IP>` with the Raspberry Pi's address, for example
`http://192.168.1.25:8080/`. The default CPU warning threshold is 60%; the
default RAM and root filesystem threshold is 70%. Configure thresholds when
starting the monitor if a demonstration needs a lower trigger, for example:

```sh
./.build/health-monitor 20 --cpu-threshold 60 --host 0.0.0.0
```

The HTTP server has no authentication. Use a trusted local network and stop the
monitor when testing is complete. Do not run CPU or memory stress tests on a
machine where the resulting load or memory pressure could interrupt important
work.

## Test 1 — CPU load escalation

**Purpose:** Exercise all four CPU cores at increasing target loads, observe
CPU utilization and warning behavior, and watch the dashboard and LED.

**Preconditions:** Install `stress-ng` (`sudo apt install stress-ng`), start the
monitor, open the dashboard, and make sure the CPU threshold is known. Close
other CPU-heavy applications. The monitor's default 60% CPU threshold is a
useful starting point.

**Steps:** In the monitor terminal, enter `t`, then `1`, and press Enter. This
runs `tests/cpu_escalation.sh`. Its equivalent command sequence is:

```sh
for load in 20 40 50 60 70 80 90; do
    echo "Testing all 4 CPU cores at ${load}%..."
    stress-ng --cpu 4 --cpu-load "$load" --timeout 10s
    sleep 2
done
```

**Expected observations:** The script reports each target. Dashboard CPU
utilization should rise during active stages and fall during the two-second
pauses. When a measured CPU sample exceeds the configured threshold, status
should become `WARNING` and the LED should turn on. Sampling continues while
the child test process runs.

**Pass:** All seven stages complete with exit status 0; dashboard CPU readings
respond to load; if a sampled reading exceeds the configured threshold, the
monitor reports `WARNING` and the LED turns on.

**Fail:** The script reports a nonzero exit, the monitor stops sampling, or a
threshold exceedance is not reflected in the displayed status/LED. A stage can
finish between samples, so a brief spike may not be observed; this alone does
not prove a monitor fault.

**Safety:** This applies substantial load to all four cores. Keep the monitor
visible, do not run during important work, and use `q` or Ctrl+C to request
shutdown. If the application is stopped during the test, it sends SIGTERM to
the test process group and reports if it must force-stop it.

## Test 2 — RAM allocation escalation

**Purpose:** Increase memory use in controlled stages and observe dashboard
memory readings and threshold status.

**Preconditions:** Install `stress-ng`, start the monitor, and keep the
dashboard visible. Close memory-heavy applications. The script requires a
readable `/proc/meminfo` with a valid `MemAvailable` value.

**Steps:** Enter `t`, then `2`, and press Enter. The script tests 256, 512,
768, and 1024 MiB in that order. Each eligible stage runs one VM worker for ten
seconds and then waits two seconds:

```sh
stress-ng --vm 1 --vm-bytes 256M --vm-keep --timeout 10s
```

The selected size changes at each stage. Before every stage the script reads
`MemAvailable` from `/proc/meminfo`. It starts a stage only when available
physical memory is at least the requested allocation **plus 1024 MiB**. For
example, the 256 MiB stage requires at least 1280 MiB available; the 1024 MiB
stage requires at least 2048 MiB available. A stage that does not meet this
condition is reported as skipped. If the kernel value is missing or invalid,
the script exits without allocating memory.

Linux `MemAvailable` estimates how much physical memory can be given to new
applications without swapping. The script does not count swap toward the
reserve. Since memory use can change after the check, this is a conservative
pre-stage guard, not a reservation enforced by the kernel. The 1024 MiB stage
is the maximum; the unsafe 3500 MiB stage is intentionally omitted.

**Expected observations:** Memory utilization should increase during stages
that start and decline afterward. With the default 70% threshold and a 4 GB
Pi, a 1024 MiB allocation may not be enough to cause `WARNING`; status depends
on the baseline, usable RAM, and configured threshold. To demonstrate a RAM
warning, start the monitor with a lower RAM threshold only if the baseline is
below it, and keep the 1024 MiB safety reserve in place.

**Pass:** At least one stage starts and completes with exit status 0, every
skipped stage prints the available and required values, and no stage starts
unless its pre-check includes the 1024 MiB reserve. If memory crosses the
configured threshold, the monitor reports `WARNING` and the LED turns on. If
all stages are skipped, the safety checks passed but the memory escalation
result is inconclusive; retry later when there is safely more available RAM.

**Fail:** The script starts a stage without enough available memory, counts
swap toward the reserve, cannot read `MemAvailable` but continues, or exits
with a stress-ng failure. A warning is only expected if a sampled memory
reading actually exceeds the configured threshold.

**Safety:** This test is capped at 1024 MiB and preserves at least 1024 MiB of
`MemAvailable` before each stage. Stop other memory-intensive work first. The
check cannot prevent unrelated processes from consuming memory after the
check, so supervise the test and stop it if the machine becomes unresponsive.

## Test 3 — Healthy baseline

**Purpose:** Confirm the monitor reports normal operation when the system is
not deliberately stressed.

**Preconditions:** Start the monitor with thresholds that are above current
CPU and memory use. Confirm root filesystem use is below its configured
threshold. Close stress programs and allow the system to settle.

**Steps:** Open the dashboard and watch at least three samples. Do not select a
stress test from the menu.

**Expected observations:** CPU, memory, and root filesystem measurements
remain visible and below their warning thresholds; status reads `HEALTHY`;
the warning LED is off. CPU may be unavailable on the first sample while its
counter baseline is established.

**Pass:** All available measurements are below threshold, status is
`HEALTHY`, and the LED remains off for the observation period.

**Fail:** A metric is above its threshold while status remains `HEALTHY`, or
the LED is on while status is `HEALTHY`. If the machine is genuinely above a
threshold, first reduce unrelated load or select appropriate thresholds
before treating the result as a monitor failure.

**Safety:** This test adds no deliberate system load and writes no test data.

## Test 4 — Recovery after stress

**Purpose:** Observe the monitor enter `WARNING` during CPU stress and recover
after the load ends.

**Preconditions:** Install `stress-ng`, start the monitor, open the dashboard,
and choose a CPU threshold low enough that the measured stress load can exceed
it. Keep the threshold and the LED visible during the run.

**Steps:** Enter `t`, then `1`, and press Enter to run the CPU escalation. Let
all stages finish. Continue watching the dashboard for several samples after
the script exits.

**Expected observations:** Status becomes `WARNING` and the LED turns on if a
sampled metric exceeds its configured threshold. After stress ends, CPU
utilization declines. Status returns to `HEALTHY` and the LED turns off only
when every available monitored metric is below its threshold.

**Pass:** The monitor reflects any sampled threshold exceedance and returns to
`HEALTHY` with the LED off after measurements recover below threshold.

**Fail:** A sampled measurement exceeds its threshold without the expected
warning/LED response, or after all available measurements are below threshold
the status/LED do not recover. Results depend on configured thresholds,
measurement timing, and actual system load; the warning may not appear if the
stress stages never exceed the chosen threshold.

**Safety:** Four CPU workers are used. Let the script finish when practical;
if shutdown is needed, the launcher reports and signals the test process group.

## Test 5 — Filesystem measurement verification

**Purpose:** Compare the dashboard's root filesystem utilization with Linux's
`df` report without consuming storage.

**Preconditions:** Start the monitor and open the dashboard. Have a second
terminal available on the Pi or connect over SSH.

**Steps:** Run:

```sh
df -h /
```

Record the dashboard's root filesystem percentage and the `Use%` value from
the `df` output at approximately the same time. The dashboard percentage is
calculated from `statvfs` total blocks and `f_bavail` (space available to
unprivileged users). Reserved filesystem blocks and filesystem-specific
accounting can therefore make it differ from `df`'s `Use%` by more than
rounding alone.

**Expected observations:** The readings refer to the same root filesystem and
should describe broadly consistent usage, but they need not match exactly.
Compare the dashboard percentage with the API's `root_filesystem.total_bytes`
and `root_filesystem.available_bytes` from `/api/metrics`, which use the same
`statvfs` values as the dashboard. Compare the API's available bytes with
`df`'s `Avail` column, allowing for `df -h` unit rounding. Interpret `df`'s
`Use%` separately because reserved blocks and filesystem accounting affect
that percentage. A stable reading consistent with those source values is a
better correctness check than exact equality with `Use%`.

**Pass:** The dashboard percentage is consistent with its API total and
available byte values, and the API available value is broadly consistent with
`df`'s `Avail` column after allowing for display rounding. The dashboard and
`df` describe the same root filesystem; an exact match with `Use%` is not
required.

**Fail:** The dashboard percentage does not agree with its API total and
available byte values, or the API's available byte value differs substantially
from `df`'s `Avail` after allowing for unit rounding and confirming both refer
to `/`. A difference between the dashboard percentage and `df`'s `Use%` alone
is not a failure.

**Safety:** This test is read-only. Do not fill the SD card, create large test
files, or otherwise intentionally consume filesystem capacity.
