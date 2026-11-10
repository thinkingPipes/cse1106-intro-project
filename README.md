# Raspberry Pi Equipment Health Monitor

A lightweight command-line monitor for Raspberry Pi and other Linux systems.
It reports CPU temperature, memory availability, root-filesystem usage, system
load, and uptime. Metrics that are not exposed by the system are shown as
unavailable rather than treated as zero.

## Project structure

- `src/main.c` — command-line entry point and report output
- `src/monitor.c` — Linux system metric collection and CSV logging
- `src/gpio_indicator.c` — optional libgpiod warning LED control
- `include/health_monitor.h` — shared monitor types and functions
- `include/gpio_indicator.h` — GPIO indicator interface
- `Makefile` — build configuration with automatic GPIO dependency detection
- `docs/` — requirements, design, and change log
- `tests/test_plan.md` — manual verification plan
- `tests/test_monitor.c` — automated status and mock GPIO tests (`make test`)
- `data/temperature_log.csv` — CSV header for optional recorded reports
- `status.txt` — current project status

## Build and run

Requires a C11 compiler and Linux system interfaces (`/proc`, `/sys`, and
`statvfs`). GPIO LED support uses libgpiod; `make` enables it automatically
when libgpiod development files are installed. Use `make GPIO=0` for a
hardware-free build, then pass `--no-gpio` or `--mock-gpio` when running the
monitor.

```sh
make
./health-monitor
```

On a development host without Raspberry Pi GPIO, build and run explicitly
without hardware:

```sh
make GPIO=0
./health-monitor --no-gpio
```

Use `--mock-gpio` to simulate GPIO 17 while still showing every LOW/HIGH
request. Unlike `--no-gpio`, mock mode exercises the status-to-LED mapping and
prints the simulated output transitions. Run the automated checks with
`make test`.

Set the memory/storage warning threshold (default: 80% used):

```sh
./health-monitor 75
```

When GPIO support is built, GPIO 17 drives the warning LED. The program turns
it on for `WARNING` or `CRITICAL`, turns it off for `HEALTHY`, and attempts to
turn it off again during cleanup. If GPIO cannot be initialized or updated,
the error is reported and the program exits non-zero after printing the
available report. `--no-gpio` skips GPIO initialization explicitly.
`--mock-gpio` records and prints output requests without touching physical
GPIO. A threshold-exceeded memory or storage reading produces `WARNING`; the
independent CPU temperature rule can still produce `CRITICAL` at 80 C. Both
`WARNING` and `CRITICAL` turn on the indicator; `HEALTHY` turns it off.

## Warning LED wiring

Connect GPIO 17 (physical header pin 11) through a 1 kΩ resistor to the LED
anode (long leg); connect the LED cathode (short leg) to a GND pin such as
physical pin 6. GPIO HIGH turns the LED on and LOW turns it off. Wire with the
Pi powered off. Raspberry Pi GPIO uses 3.3 V logic; do not connect a 5 V rail
to a GPIO pin. The chip defaults to `/dev/gpiochip0`; if the system assigns the
GPIO controller a different device path, set `HEALTH_MONITOR_GPIO_CHIP`.

Install the libgpiod development package for the OS before building the
hardware-enabled version. The process must have permission to open the GPIO
chip and request GPIO 17; use the device's normal GPIO access group or the
course-approved elevated command if permissions require it. Do not run as root
unless the lab specifically requires it.

Append each report to a CSV file:

```sh
./health-monitor --log data/temperature_log.csv
```

Use `./health-monitor --help` to see command-line options. The temperature
sensor is read from `/sys/class/thermal/thermal_zone0/temp`; systems without
that sensor still report the other available metrics.

## Health thresholds

- CPU temperature: warning at 70 C, critical at 80 C
- Memory usage: warning when utilization exceeds the configured percentage
- Root filesystem usage: warning when utilization exceeds the configured percentage

The memory/storage threshold defaults to 80% used and can be set to any
integer percentage from 1 to 100 as the first positional argument. A resource
exactly at the threshold does not trigger a warning. The same threshold applies
to memory and root filesystem usage.

Unavailable metrics are omitted from threshold evaluation. These initial
thresholds are intended as sensible defaults and should be reviewed for the
target equipment.
