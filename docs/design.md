# Design

The program is a small C11 command-line application. `src/main.c` parses
`--help` and the optional `--log FILE` argument, requests one health snapshot,
prints it, and optionally appends it to a CSV file.

`src/monitor.c` reads CPU temperature from Linux thermal sysfs, memory/load/
uptime from `/proc`, and root filesystem capacity through `statvfs`. Each
metric is tracked separately so an unavailable sensor does not invalidate
other readings. The status evaluator checks CPU temperature, available memory,
and root filesystem free space against the initial thresholds in the README.
Load and uptime are informational.

The CSV log contains a UTC timestamp, the reported readings, and overall
status. Unsupported values are left blank. `include/health_monitor.h` defines
the report structure shared by the entry point and collector.
