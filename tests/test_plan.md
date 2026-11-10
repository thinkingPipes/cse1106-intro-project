# Test Plan

## Build and command-line behavior

- Build the application and test program with C11 and warnings enabled using
  `make` and `make test`.
- Run `./health-monitor --help` and verify usage is printed successfully.
- Run with an unknown option and verify it exits with a non-zero status.
- Run on the Raspberry Pi with the GPIO LED wired; verify reports repeat at the
  configured interval and SIGINT/SIGTERM shuts down cleanly.
- Run with `75` and verify the report displays a 75% memory/storage threshold.
- Run with invalid thresholds (`0`, `101`, `75.5`, or text) and verify a useful
  error and non-zero exit status.

## Metrics and status

- On Linux, compare memory and filesystem values with their corresponding
  system interfaces.
- Verify unavailable readings are not printed as zero.
- Verify memory and storage each warn when utilization exceeds the configured
  threshold and remain healthy at exactly the threshold using controlled data.

## CSV logging

- Log to a new file and verify one row is appended per monitoring interval.
- Verify the header appears only once across multiple samples.
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
