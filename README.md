# Raspberry Pi Equipment Health Monitor

A C11 Linux monitor for CPU, memory, and root-filesystem utilization. It
evaluates health thresholds, controls a physical warning LED on BCM GPIO 17,
optionally appends reports to CSV, and serves a small browser entry page plus a
read-only JSON API through an embedded CivetWeb server.

## Architecture

- `src/monitor.c` collects CPU, memory, and root-filesystem metrics, evaluates
  status, and appends CSV reports.
- `src/gpio_indicator.c` owns GPIO 17 setup, status updates, and cleanup.
- `src/web_server.c` embeds CivetWeb, serves static assets, and provides HTTP
  handlers. `src/web_api.c` serializes report snapshots as JSON.
- `src/main.c` coordinates sampling, GPIO updates, CSV logging, and the web
  server. Request handlers read a mutex-protected copy of the latest report.
- `web/` contains the responsive dashboard. It reads current health and metric
  data from `/api/health` and `/api/metrics`; trend points are collected only
  during the browser session.

## Build and run

Install a C11 compiler, Make, pkg-config, libgpiod development files, and
CivetWeb development files. The Makefile expects pkg-config modules named
`libgpiod` and `civetweb`.

```sh
make
./.build/health-monitor
```

The server defaults to `127.0.0.1:8080`, so it is only reachable from the Pi
itself. `--host` accepts an IPv4 address. To access it from another computer
on the local network, bind to all IPv4 interfaces and use the Pi's LAN address
in the browser:

```sh
./.build/health-monitor --host 0.0.0.0 --port 8080
```

The HTTP service has no authentication and exposes read-only health data and
the static files under `web/`. Bind only to a trusted network and use firewall
rules to limit access. No HTTP endpoint controls GPIO or changes monitor
configuration. Use `--web-root DIR` if the process is started outside the
project directory.

The sample interval defaults to 5 seconds. The RAM/root-filesystem warning
threshold defaults to 70%; CPU has a separate 60% default. A warning is raised
only when a measured value is above its threshold. CPU utilization is
unavailable on the first sample while `/proc/stat` establishes its baseline.

```sh
./.build/health-monitor 70 --cpu-threshold 85 --interval 10 --log data/health.csv
```

Use `./.build/health-monitor --help` for all options. Ctrl+C or SIGTERM stops the
server, turns the warning LED off, and releases GPIO resources.

## HTTP API contract

The backend contract, exact field types and units, null behavior, error
responses, and representative payloads are documented in
[docs/api.md](docs/api.md). Both endpoints return the latest completed sample;
request handlers do not collect metrics.

## Warning LED

Connect GPIO 17 (physical header pin 11) through a 1 kΩ resistor to the LED
anode; connect the cathode to GND. GPIO HIGH indicates `WARNING`. The chip
defaults to `/dev/gpiochip0`; set `HEALTH_MONITOR_GPIO_CHIP` if needed. The
process must have permission to request GPIO 17. The program exits if GPIO
initialization fails and reports update or cleanup failures.

## Checks

```sh
make test
```

The test target exercises health thresholds, mock GPIO behavior, CSV handling,
and the JSON serializer. It does not require CivetWeb, ncurses, or physical GPIO.

To build the complete server and sampling loop with mock GPIO for local HTTP
integration checks, run `make integration-build`; start it with
`./.build/health-monitor-mock`. This verifies the software path without
accessing GPIO hardware and does not replace the normal `health-monitor`
executable.
