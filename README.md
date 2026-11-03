# Raspberry Pi Equipment Health Monitor

A lightweight command-line monitor for Raspberry Pi and other Linux systems.
It reports CPU temperature, memory availability, root-filesystem usage, system
load, and uptime. Metrics that are not exposed by the system are shown as
unavailable rather than treated as zero.

## Project structure

- `src/main.c` — command-line entry point and report output
- `src/monitor.c` — Linux system metric collection and CSV logging
- `include/health_monitor.h` — shared monitor types and functions
- `docs/` — requirements, design, and change log
- `tests/test_plan.md` — manual verification plan
- `data/temperature_log.csv` — CSV header for optional recorded reports
- `status.txt` — current project status

## Build and run

Requires a C11 compiler and Linux system interfaces (`/proc`, `/sys`, and
`statvfs`).

```sh
gcc -Wall -Wextra -Wpedantic -std=c11 -Iinclude \
    src/main.c src/monitor.c -o health-monitor
./health-monitor
```

Append each report to a CSV file:

```sh
./health-monitor --log data/temperature_log.csv
```

Use `./health-monitor --help` to see command-line options. The temperature
sensor is read from `/sys/class/thermal/thermal_zone0/temp`; systems without
that sensor still report the other available metrics.

## Health thresholds

- CPU temperature: warning at 70 C, critical at 80 C
- Available memory: warning below 15%, critical below 5%
- Root filesystem free space: warning below 15%, critical below 5%

Unavailable metrics are omitted from threshold evaluation. These initial
thresholds are intended as sensible defaults and should be reviewed for the
target equipment.
