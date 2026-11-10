# Test Plan

## Build and command-line behavior

- Build the application and test program with C11 and warnings enabled using
  `make` and `make test`.
- Run `./health-monitor --help` and verify usage is printed successfully.
- Run with an unknown option and verify it exits with a non-zero status.
- Run with `--no-gpio` or `--mock-gpio` and verify the report prints
  successfully on a host without GPIO hardware.
- Run with `75` and verify the report displays a 75% memory/storage threshold.
- Run with invalid thresholds (`0`, `101`, `75.5`, or text) and verify a useful
  error and non-zero exit status.

## Metrics and status

- On Linux, compare memory, load, uptime, temperature, and filesystem values
  with their corresponding system interfaces where available.
- On a host without a thermal zone sensor, verify temperature is `N/A` while
  other metrics are still reported.
- Verify unavailable readings are not printed as zero.
- Verify memory and storage each warn when utilization exceeds the configured
  threshold and remain healthy at exactly the threshold using controlled data.

## CSV logging

- Log to a new file and verify a header and one data row are written.
- Log a second time and verify the header is not repeated.
- Verify unavailable readings produce empty CSV fields.
- Attempt to log to an invalid path and verify the program reports an error
  and exits non-zero.

## GPIO warning LED

- On the Raspberry Pi with the LED wired to GPIO 17 through a 1 kΩ resistor,
  verify a `HEALTHY` report leaves the LED off and a `WARNING` report turns it
  on.
- Verify `--no-gpio` runs the report without requesting GPIO hardware.
- With GPIO support built but unavailable hardware or permissions, verify the
  program reports the LED failure and exits non-zero.
- Verify the LED turns off when the program finishes after a warning report.
- Run `make test` to exercise resource threshold decisions, status-to-LED
  mapping, GPIO initialization errors, and mock cleanup on a host.
