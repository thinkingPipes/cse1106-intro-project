# Change Log

## Raspberry Pi Equipment Health Monitor

- Replaced the ncurses presentation with an embedded CivetWeb server on the
  `feature/web-server` milestone branch.
- Added static placeholder assets under `web/` and read-only JSON health and
  metrics endpoints with mutex-protected report snapshots.
- Added configurable host, port, and static root options. The default listener
  is loopback; LAN binding is opt-in.
- Removed ncurses source and build dependencies; added JSON serializer tests.
- Updated requirements, design, README, test plan, and status to match the
  implemented CPU/RAM/root-filesystem monitoring behavior.

- Replaced the fortune-generator example with Linux equipment health reporting.
- Added temperature, memory, disk, load, and uptime collection.
- Added threshold-based health status and optional CSV logging.
- Renamed the sample data file to `temperature_log.csv`.
- Updated the requirements, design, README, test plan, and project status.
- Added a mock GPIO backend and automated tests for status thresholds, LED
  mapping, initialization failures, and cleanup.
