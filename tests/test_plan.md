# Test Plan

## Build and command-line behavior

- Build the production application with `make` on a system with libgpiod and
  CivetWeb development packages installed as described in the README.
- Run `./.build/health-monitor --help`; verify host, port, web root, thresholds,
  interval, and CSV options are documented.
- Verify invalid ports, thresholds, intervals, duplicate options, unknown
  options, and incomplete options fail with a non-zero status.
- Verify the default bind is `127.0.0.1:8080`; verify `--host 0.0.0.0`
  allows a second computer on the local network to connect.
- Verify SIGINT and SIGTERM stop CivetWeb, turn the LED off, and release GPIO.
- Verify the interactive `h` help, `t` test menu, and `q` shutdown controls.
- Verify test selections 3–5 print their instructions and point to
  `docs/test-cases.md`.

## HTTP and static assets

- Verify `/` serves `web/index.html`, and `/style.css` and `/app.js` use their
  expected static content types.
- Verify `GET /api/health` returns HTTP 200 and JSON with service `ok`, overall
  status, and an ISO-8601 UTC sample timestamp.
- Verify `GET /api/metrics` returns HTTP 200 and JSON for CPU, memory, and root
  filesystem data, availability flags, units, and thresholds.
- With `--log FILE`, verify `GET /api/history?limit=10` returns valid recent
  CSV records; without a log path, verify it returns an empty sample list.
- Verify unavailable measurements are `null`, not zero; API methods other than
  GET return 405 and unknown routes return 404.
- Verify requests during sampling receive a complete previous or current
  report, never a partially updated report.
- Verify static assets and API requests work when `--web-root DIR` is used.
- Verify LAN binding only on a trusted network; API endpoints are read-only and
  unauthenticated.

## Metrics, CSV, and GPIO

- Compare CPU, memory, and filesystem readings with their Linux sources.
- Verify first CPU sample is unavailable, later samples use aggregate
  `/proc/stat` deltas, and invalid/reset counters do not yield bogus values.
- Verify memory, storage, and CPU warnings trigger only above configured
  thresholds and warning status maps to GPIO HIGH.
- Log to a new CSV file and verify one row per sample, correct headers,
  unavailable fields, and useful errors for invalid paths or legacy schemas.
- Run `make test` to exercise thresholds, mock GPIO behavior, CSV handling,
  and JSON serialization without CivetWeb or physical GPIO.
- Run `make integration-build` on a host without GPIO, then use
  `./.build/health-monitor-mock` to verify the real CivetWeb server and
  sampling loop with mock GPIO output.
- With harmless fake `stress-ng` and `sleep` commands earlier on `PATH`, verify
  test child output, completion status, continued sampling, and `q`/SIGINT
  shutdown of an active child process group. Do not use real stress commands
  for this software-only check.
- Run `tests/test_sampling_interval.sh` after `make integration-build`. It uses
  mock GPIO and fake stress commands to verify a five-second interval, live
  keyboard input, child completion, and sampling while a child test runs.
- Run `sh -n` on `tests/cpu_escalation.sh`, `tests/ram_escalation.sh`, and
  `tests/test_sampling_interval.sh` to check shell syntax without starting a
  real stress test.

## Raspberry Pi manual verification

- On hardware with GPIO 17 wired, verify the LED is HIGH for WARNING, LOW for
  other statuses, and LOW after SIGINT/SIGTERM cleanup.
- Verify another computer can load the page and API over the local network
  after binding to the Pi's LAN interface.
