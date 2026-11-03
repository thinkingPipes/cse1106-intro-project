# Requirements

## Purpose

Provide a simple command-line health snapshot for a Raspberry Pi or compatible
Linux machine.

## Functional requirements

- Report CPU temperature, available memory, root filesystem usage, one-minute
  system load, and uptime when the operating system exposes those metrics.
- Show unavailable readings as `N/A`; do not present missing values as zero.
- Evaluate temperature, available-memory, and free-disk thresholds and display
  an overall `HEALTHY`, `WARNING`, `CRITICAL`, or `UNKNOWN` status.
- Optionally append each report to a CSV file using `--log FILE`.
- Print usage information with `--help` and reject unknown or incomplete
  command-line options.

## Platform requirements

- Linux with a C11 compiler.
- Read access to `/proc/meminfo`, `/proc/loadavg`, `/proc/uptime`,
  `/sys/class/thermal/thermal_zone0/temp`, and root filesystem statistics.
- Missing individual sources must not prevent other available readings from
  being reported.
