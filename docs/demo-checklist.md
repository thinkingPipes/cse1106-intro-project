# Raspberry Pi Hardware Demonstration Checklist

This procedure covers the production monitor, its GPIO warning LED, and the
browser dashboard. Hardware and browser steps are acceptance checks to perform
on the actual equipment; they are not recorded as passed by this document.

## 1. Build preparation

Install the platform's development packages for:

- A C11 compiler, such as GCC or Clang.
- GNU Make.
- `pkg-config`.
- libgpiod development headers and library; the Makefile looks for the
  `libgpiod` pkg-config module.
- CivetWeb development headers and library; the Makefile looks for the
  `civetweb` pkg-config module.

Package names differ between Linux distributions. Verify that both modules
are visible before building:

```sh
pkg-config --modversion libgpiod civetweb
```

From the project directory, build the production application and optionally
run the automated tests:

```sh
make
make test
```

The production binary is `./.build/health-monitor`. It initializes physical
GPIO 17 through libgpiod and exits if it cannot initialize GPIO. The Makefile's
default target writes under `.build/`; it does not overwrite the tracked
`health-monitor` binary.

For software-only local checks, build and run the mock-GPIO integration
application:

```sh
make integration-build
./.build/health-monitor-mock
```

This build uses the same sampling and HTTP code, but prints mock GPIO state
instead of controlling a pin. It is not a substitute for the production build
or a physical LED test. Do not use the mock build to claim hardware acceptance.

## 2. Start the application and connect

Connect to the Pi locally or by SSH, change to the project directory, and
launch the production binary bound to its LAN interfaces:

```sh
./.build/health-monitor --host 0.0.0.0 --port 8080 --web-root web
```

The default sample interval is 5 seconds. The default warning thresholds are
60% for CPU and 70% for memory and root filesystem. To use custom thresholds,
the first positional percentage sets the shared memory/storage limit and
`--cpu-threshold` sets CPU's limit. For example:

```sh
./.build/health-monitor 1 --cpu-threshold 1 --host 0.0.0.0 --port 8080 --web-root web
```

That low-threshold command is an optional warning demonstration: the LED turns
on only if the monitor has an available measurement above a configured limit.
It does not fabricate a warning or add load. Return to the default command
after the demo.

In another Pi terminal, find its LAN addresses:

```sh
hostname -I
```

If that command is unavailable, use `ip -4 -brief address` and identify the
Pi's address on the trusted LAN. From a computer on that same network, open:

```text
http://<pi-lan-ip>:8080/
```

For example, replace `<pi-lan-ip>` with the Pi's actual address. The server's
default binding is loopback only (`127.0.0.1`); `--host 0.0.0.0` is necessary
for another computer to connect. Permit port 8080 through the local firewall
only on the trusted private network, if a firewall is enabled.

## 3. Hardware and warning LED verification

With the Pi powered off before wiring:

1. Connect BCM GPIO 17, physical header pin 11, through a 1 kΩ resistor to the
   LED anode (long leg).
2. Connect the LED cathode (short leg) to a Pi GND pin, such as physical pin 6.
3. Inspect the circuit before powering the Pi. GPIO uses 3.3 V logic: never
   connect 5 V to GPIO 17.
4. Confirm the application has permission to access the GPIO chip. It defaults
   to `/dev/gpiochip0`; if the Pi assigns another chip path, set
   `HEALTH_MONITOR_GPIO_CHIP` when launching the production program.

Observe the LED together with the dashboard and console:

- `HEALTHY` drives GPIO 17 LOW and the warning LED should be off.
- `WARNING` drives GPIO 17 HIGH and the warning LED should be on.
- `UNKNOWN` is not a warning status and drives the LED LOW.
- Warning status is raised when an available reading is **above** its
  threshold. A reading equal to the threshold remains within the limit.

For a reliable demonstration, first observe the default thresholds. If no
resource exceeds them, optionally relaunch with the 1% thresholds shown above
and verify the dashboard reports `WARNING` and the LED turns on. Do not claim
warning hardware verification unless the physical LED response was observed.

To verify shutdown, press Ctrl+C in the application's terminal or send it
SIGTERM. Confirm the server stops, the LED turns off, and the terminal reports
`Monitoring stopped; warning LED is off.` The application explicitly requests
GPIO LOW during cleanup before releasing its libgpiod request. After release,
the process no longer owns or actively drives the GPIO line; do not treat the
post-exit pin voltage as a guaranteed driven LOW state. Do not disconnect or
rewire the LED while the Pi is powered.

## 4. Dashboard acceptance checks

Use the dashboard while the production process is running:

- Confirm the connection indicator reports connected independently of the
  equipment health badge.
- Confirm the overall status is one of `HEALTHY`, `WARNING`, or `UNKNOWN` and
  agrees with the latest completed sample.
- Confirm CPU, memory, and root filesystem cards show percentages when
  available, and show an unavailable state rather than zero when readings are
  null or unavailable.
- Confirm each card shows its API warning threshold. Defaults are CPU 60% and
  memory/root filesystem 70% unless changed on the command line.
- Confirm the pressure summary ranks available resources by their value
  relative to their warning threshold and excludes unavailable resources.
- Confirm the trend chart uses samples observed by this browser session and
  shows the API-provided warning limits. Expand **View observed chart data as a
  table** and check sample times, percentages, and unavailable cells.
- Confirm the trend label identifies browser-session samples and does not
  imply persistent history. The browser retains at most 360 distinct samples.
- Confirm the displayed sample time is derived from the API's UTC timestamp and
  presented in the browser's local time zone.

To check stale and disconnected behavior, stop sampling or stop the server
after the page has received at least one sample. A sample older than 15 seconds
should be marked stale. When requests fail, the dashboard should show a
disconnected state and mark any retained last readings as stale. Restart the
server and confirm current readings resume. These checks exercise display
behavior; do not mistake a stopped server for a live hardware warning test.

The dashboard polls current health and metrics every 5 seconds. Its session
table and chart are generated from distinct timestamps observed while that
page is open; they are not backend-persisted historical records.

## 5. First-demonstration pass/fail checklist

Mark each item only after observing it on the actual setup:

- [ ] **Build:** `make` completes on the Pi and creates `.build/health-monitor`.
- [ ] **Startup:** the production program starts, samples metrics, and binds to
      the requested LAN address and port.
- [ ] **Remote dashboard:** another computer on the trusted LAN opens the page
      at `http://<pi-lan-ip>:8080/`.
- [ ] **Current data:** CPU, memory, root filesystem, thresholds, and sample
      time match the API response; unavailable data is not displayed as zero.
- [ ] **Health/connectivity:** dashboard health agrees with the monitor, and
      connection state remains a separate indication.
- [ ] **Healthy LED:** with `HEALTHY` status, the physical LED is off.
- [ ] **Warning LED:** with at least one available metric above threshold, the
      status is `WARNING` and the physical LED is on.
- [ ] **Trend/table:** browser-session samples accumulate, warning-limit lines
      appear, and the table shows corresponding values and unavailable cells.
- [ ] **Stale/disconnected:** an old sample is marked stale; stopped-server
      behavior shows disconnection and stale retained readings; recovery works.
- [ ] **Clean shutdown:** Ctrl+C or SIGTERM stops the server, the LED turns off,
      and the shutdown message confirms cleanup.
- [ ] **No hardware claim without observation:** record any untested item as
      pending rather than passed.

## 6. Known limitations

- The historical `GET /api/history` endpoint is excluded from the dashboard.
  Its optimized-build HTTP crash is unresolved; the current dashboard uses
  browser-session observations only. Do not use the history endpoint for this
  demonstration.
- Browser rendering has not yet been visually verified. Check the actual Pi
  browser view at desktop and phone widths during acceptance.
- The HTTP service has no authentication. Keep it on a trusted private LAN,
  restrict access with firewall rules where appropriate, and do not expose it
  to the public internet.
- Chart.js and the selected web fonts are loaded from external CDNs. The
  browser needs access to those CDNs for the intended chart and typography;
  their rendering and availability remain part of browser verification.
- This checklist describes tests to perform; no physical GPIO or complete
  hardware demonstration is claimed as passed here.
