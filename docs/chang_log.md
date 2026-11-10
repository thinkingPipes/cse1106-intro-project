# Change Log

## Raspberry Pi Equipment Health Monitor

- Replaced the fortune-generator example with Linux equipment health reporting.
- Added temperature, memory, disk, load, and uptime collection.
- Added threshold-based health status and optional CSV logging.
- Renamed the sample data file to `temperature_log.csv`.
- Updated the requirements, design, README, test plan, and project status.
- Added a mock GPIO backend and automated tests for status thresholds, LED
  mapping, initialization failures, and cleanup.
