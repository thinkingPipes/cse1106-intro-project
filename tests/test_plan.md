# Test Plan

## Build and command-line behavior

- Compile both source files with C11 and warnings enabled.
- Run `./health-monitor --help` and verify usage is printed successfully.
- Run with an unknown option and verify it exits with a non-zero status.
- Run without options and verify the report prints successfully.

## Metrics and status

- On Linux, compare memory, load, uptime, temperature, and filesystem values
  with their corresponding system interfaces where available.
- On a host without a thermal zone sensor, verify temperature is `N/A` while
  other metrics are still reported.
- Verify unavailable readings are not printed as zero.
- Exercise the warning and critical thresholds on a test device or with
  controlled test data before relying on alerts in production.

## CSV logging

- Log to a new file and verify a header and one data row are written.
- Log a second time and verify the header is not repeated.
- Verify unavailable readings produce empty CSV fields.
- Attempt to log to an invalid path and verify the program reports an error
  and exits non-zero.
