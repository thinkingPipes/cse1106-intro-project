# Test Plan

## Build and command-line behavior

- Build the application and test program with C11 and warnings enabled using
  `make` and `make test`.
- Run `./health-monitor --help` and verify usage is printed successfully.
- Run with an unknown option and verify it exits with a non-zero status.
- Run on the Raspberry Pi with the GPIO LED wired; verify reports repeat at the
  configured interval and SIGINT/SIGTERM shuts down cleanly.
- In an interactive terminal, verify the ncurses dashboard redraws after a
  resize, shows a small-terminal message when below its minimum size, and falls
  back to text output when no usable terminal is available.
- Verify dashboard CPU/RAM/storage bars, CPU history, CPU-temperature N/A,
  status colors, and timestamp update from each monitoring report.
- Run with `75` and verify the report displays a 75% memory/storage threshold.
- Verify the default RAM/storage threshold is 70% and `--cpu-threshold` changes
  the separate CPU warning threshold.
- Run with invalid thresholds (`0`, `101`, `75.5`, or text) and verify a useful
  error and non-zero exit status.

## Metrics and status

- On Linux, compare memory and filesystem values with their corresponding
  system interfaces.
- Verify unavailable readings are not printed as zero.
- Verify memory and storage each warn when utilization exceeds the configured
  threshold and remain healthy at exactly the threshold using controlled data.
- Verify the first CPU sample reports unavailable, later samples use aggregate
  `/proc/stat` counter deltas, and invalid/reset counters do not yield a bogus
  percentage.
- Verify CPU threshold equality remains healthy, exceeding it warns, and a CPU
  warning drives the physical LED through the existing status mapping.

## CSV logging

- Log to a new file and verify one row is appended per monitoring interval.
- Verify the header appears only once across multiple samples.
- Verify CPU utilization and its separate warning threshold are logged.
- Use a new CSV filename after the CSV schema change; verify legacy logs are
  rejected without being modified.
- Verify unavailable readings produce empty CSV fields.
- Attempt to log to an invalid path and verify the program reports an error
  and exits non-zero.

## GPIO warning LED

- On the Raspberry Pi with the LED wired to GPIO 17 through a 1 kΩ resistor,
  verify a `HEALTHY` report leaves the LED off and a `WARNING` report turns it
  on.
- With GPIO support built but unavailable hardware or permissions, verify the
  program reports the LED failure and exits non-zero.
- Verify SIGINT and SIGTERM turn the LED off before process exit.
- Run `make test` to exercise resource threshold decisions, status-to-LED
  mapping, GPIO initialization errors, and mock cleanup on a host.
- Verify `make test` remains independent of ncurses and does not initialize a
  terminal.
