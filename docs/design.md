# Design

The program is a small C11 command-line application. `src/main.c` parses
`--help`, the optional `--log FILE` argument, and an optional integer
memory/storage warning threshold (default 80%, range 1–100). It requests one
health snapshot, prints it, and optionally appends it to a CSV file.

`src/monitor.c` reads CPU temperature from Linux thermal sysfs, memory/load/
uptime from `/proc`, and root filesystem capacity through `statvfs`. Each
metric is tracked separately so an unavailable sensor does not invalidate
other readings. The status evaluator checks CPU temperature, available memory,
and root filesystem utilization. Load and uptime are informational.

Memory and root filesystem utilization are both evaluated as percentage used;
either exceeding the configured threshold causes a warning, while equality
does not. CPU temperature retains its existing independent warning/critical
thresholds. The CSV log contains a UTC timestamp, the reported readings,
configured threshold, and overall status. Unsupported values are left blank.
`include/health_monitor.h` defines the report structure shared by the entry
point and collector.

`src/gpio_indicator.c` owns GPIO setup, output changes, and cleanup, separate
from metric collection. It uses libgpiod on hardware-enabled builds. The
application requests GPIO 17 as an output initialized LOW, then applies the
report status to the LED. `--mock-gpio` uses a mock backend that prints and
records HIGH/LOW requests without physical hardware; `--no-gpio` skips the
indicator entirely. A memory or storage threshold exceedance produces
`WARNING`; the independent CPU-temperature rule can produce `CRITICAL`, which
also lights the LED. Failed hardware initialization is reported and yields a
non-zero exit status.
