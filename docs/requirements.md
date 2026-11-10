# Requirements

## Purpose

Provide a simple command-line health snapshot for a Raspberry Pi or compatible
Linux machine.

## Functional requirements

- Report CPU temperature, available memory, root filesystem usage, one-minute
  system load, and uptime when the operating system exposes those metrics.
- Show unavailable readings as `N/A`; do not present missing values as zero.
- Compare memory and root-filesystem utilization (percentage used) against one
  configurable warning threshold, defaulting to 80% and accepting integer
  values from 1 through 100. Warn when either resource exceeds the threshold;
  an exact match remains healthy.
- Evaluate CPU temperature thresholds and display an overall `HEALTHY`,
  `WARNING`, `CRITICAL`, or `UNKNOWN` status.
- Optionally append each report to a CSV file using `--log FILE`.
- Drive GPIO 17 HIGH for `WARNING` (and `CRITICAL`) and LOW for `HEALTHY`;
  attempt to leave it LOW during cleanup.
- Support an explicit `--no-gpio` mode for development without physical GPIO.
- Support `--mock-gpio` to simulate and display GPIO output requests without
  physical hardware.
- Report GPIO initialization or update failures and exit non-zero rather than
  claiming the LED operated successfully.
- Print usage information with `--help` and reject invalid thresholds,
  unknown options, or incomplete options.

## Platform requirements

- Linux with a C11 compiler.
- Read access to `/proc/meminfo`, `/proc/loadavg`, `/proc/uptime`,
  `/sys/class/thermal/thermal_zone0/temp`, and root filesystem statistics.
- Missing individual sources must not prevent other available readings from
  being reported.
