# Raspberry Pi Equipment Health Monitor

A lightweight Linux monitor for memory and root-filesystem utilization. It reports `HEALTHY` or `WARNING` using one configurable utilization threshold, controls a physical warning LED on BCM GPIO 17, and can append each report to a CSV log.

## Project structure

- `src/main.c` — command-line options, continuous sampling, signal handling, and output coordination
- `src/monitor.c` — memory and filesystem collection, status evaluation, and CSV logging
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

Change the shared memory/storage warning threshold (default 80%) or sampling interval (1–3600 seconds):

```sh
./health-monitor 75 --interval 10
```

Append one CSV report per sample:

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

Memory use is calculated from `MemTotal` and `MemAvailable`; storage use is measured for the root filesystem. Either resource above the configured threshold produces `WARNING`; values at or below it produce `HEALTHY`. The threshold defaults to 80% and accepts integer values from 1 to 100. A resource exactly at the threshold does not trigger a warning. If both readings are unavailable, status is `UNKNOWN`.
