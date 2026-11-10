# Raspberry Pi Equipment Health Monitor

A lightweight Linux monitor for CPU, memory, and root-filesystem utilization. It reports `HEALTHY` or `WARNING`, controls a physical warning LED on BCM GPIO 17, and can append each report to a CSV log.

## Project structure

- `src/main.c` — command-line options, continuous sampling, signal handling, and output coordination
- `src/monitor.c` — CPU, memory, and filesystem collection, status evaluation, and CSV logging
- `src/gpio_indicator.c` — physical GPIO 17 LED control through libgpiod
- `include/` — shared monitor and GPIO interfaces
- `Makefile` — build configuration requiring libgpiod development files
- `docs/` — requirements, design, and change log
- `tests/` — automated status/GPIO tests and manual verification plan

## Build and run

Build on the Raspberry Pi with a C11 compiler, Make, and libgpiod development files:

```sh
make
```

Run continuously in the foreground. The default sampling interval is 5 seconds, and Ctrl+C stops the monitor cleanly:

```sh
./health-monitor
```

The RAM and root-filesystem threshold defaults to 70%. CPU has a separate 60% default. Set them independently; the first positional number is the RAM/storage threshold, and `--cpu-threshold` sets the CPU threshold. The sampling interval defaults to 5 seconds:

```sh
./health-monitor 70 --cpu-threshold 85 --interval 10
```

CPU utilization is unavailable on the initial sample because it establishes a counter baseline; valid CPU percentages appear from the next sample onward. Append one CSV report per sample:

```sh
./health-monitor --log data/temperature_log.csv
```

Run continuously in the background and save console output:

```sh
nohup ./health-monitor --log data/temperature_log.csv > health-monitor.out 2>&1 & echo $! > health-monitor.pid
```

Stop the background process with SIGTERM so it can turn the LED off and release GPIO:

```sh
kill -TERM "$(cat health-monitor.pid)"
```

Run automated checks with `make test`.

## Warning LED wiring

Connect GPIO 17 (physical header pin 11) through a 1 kΩ resistor to the LED anode (long leg); connect the LED cathode (short leg) to a GND pin such as physical pin 6. GPIO HIGH turns the LED on and LOW turns it off. Wire with the Pi powered off. Raspberry Pi GPIO uses 3.3 V logic; do not connect a 5 V rail to a GPIO pin. The chip defaults to `/dev/gpiochip0`; if the system assigns the GPIO controller a different device path, set `HEALTH_MONITOR_GPIO_CHIP`.

Install the libgpiod development package before building. The process must have permission to open the GPIO chip and request GPIO 17. The application reports GPIO errors and exits if physical GPIO initialization fails. The LED is on while either memory or storage is above the threshold and is turned off during normal shutdown.

## Health threshold

Memory use is calculated from `MemTotal` and `MemAvailable`; storage use is measured for the root filesystem. Either resource above the shared 70% default threshold produces `WARNING`. CPU utilization uses the aggregate `cpu` counters in `/proc/stat` and a separate 60% default threshold. A warning is raised only when a measured value is above its threshold. CPU counters are cumulative, so the first sample is a baseline; missing or invalid counter readings are not treated as zero. Both thresholds accept integer values from 1 to 100. If all three readings are unavailable, status is `UNKNOWN`.

CSV output now includes CPU utilization and its threshold. Existing logs with the older header are left unchanged and rejected for appending; choose a new log filename when using the updated CSV format.
