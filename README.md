# Raspberry Pi Equipment Health Monitor

A C11 Linux monitor for CPU, memory, and root-filesystem utilization. It
evaluates health thresholds, controls a physical warning LED on BCM GPIO 17,
optionally appends reports to CSV, and serves a browser dashboard plus a
read-only JSON API through an embedded CivetWeb server.

The demonstration target is a Raspberry Pi with four CPU cores, 4 GB RAM, and
64 GB storage. Production mode requires an accessible BCM GPIO 17 warning LED
(physical header pin 11) wired through a current-limiting resistor. Use the
mock integration build for software-only checks without GPIO hardware.

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

## Build prerequisites

On Raspberry Pi OS, Debian, or Ubuntu, install the compiler, Make, pkg-config,
and the GPIO and CivetWeb development packages:

```sh
sudo apt update
sudo apt install build-essential pkg-config libgpiod-dev libcivetweb-dev
```

The Makefile gets libgpiod flags through its `libgpiod` pkg-config module. For
CivetWeb it uses a `civetweb` pkg-config module when available, or links the
system header and library directly (`-lcivetweb`) on distributions whose
development package does not provide a `.pc` file. On Ubuntu, make sure the
Universe repository is enabled if `apt` cannot find these development
packages. Install stress-ng only if you plan to run the CPU or RAM test:

```sh
sudo apt install stress-ng
```

For automatic desktop browser opening, `xdg-open` is optional and is commonly
provided by `xdg-utils`; the launcher always prints the URL for manual use.

## Build and run

Run the following commands from the project directory. `make test` needs only
the compiler and pthread support; the production build also needs the
libgpiod and CivetWeb development packages above.

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
project directory. If you start the program outside the project directory, the
test scripts still require the project directory as the current working
directory. The dashboard loads Chart.js and its selected fonts from
external CDNs; the monitor and API are local, while charts and font styling
require the browser to reach those CDN hosts.

The sample interval defaults to 5 seconds. The RAM/root-filesystem warning
threshold defaults to 70%; CPU has a separate 60% default. A warning is raised
only when a measured value is above its threshold. CPU utilization is
unavailable on the first sample while `/proc/stat` establishes its baseline.

```sh
./.build/health-monitor 70 --cpu-threshold 85 --interval 10 --log data/health.csv
```

Use `./.build/health-monitor --help` for all options. Ctrl+C or SIGTERM stops the
server, turns the warning LED off, and releases GPIO resources.

The interactive terminal stays available as a control panel:

- `h` shows launcher commands.
- `t` opens the hardware test menu; enter `1`–`5` to select a test.
- Tests 1 and 2 run CPU and RAM stress scripts as child processes. Monitoring
  and LED updates continue while they run.
- Test 2 checks `MemAvailable` before each stage and requires the requested
  allocation plus a 1024 MiB reserve. Tests 3–5 show manual instructions.
- `q` gracefully stops an active test, the web server, and the monitor.

Start the application from the project directory so its `web/` assets and test
scripts are available. The launcher may use `xdg-open` when a graphical session
is detected; otherwise use the printed URL. Full prerequisites, procedures,
safety notes, and pass/fail criteria are in
[docs/test-cases.md](docs/test-cases.md).

## HTTP API contract

The backend contract, exact field types and units, null behavior, error
responses, and representative payloads are documented in
[docs/api.md](docs/api.md). The health and metrics endpoints return the latest
completed sample; request handlers do not collect metrics. The history
endpoint reads recent rows from the configured CSV log.

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
and JSON serialization. It does not require CivetWeb or physical GPIO. There
is no ncurses or other full-screen terminal dependency.

To build the complete server and sampling loop with mock GPIO for local HTTP
integration checks, run `make integration-build`; start it with
`./.build/health-monitor-mock`. This verifies the software path without
accessing GPIO hardware and does not replace the normal `health-monitor`
executable.
