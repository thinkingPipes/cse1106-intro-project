# Change Log

## Current submission

- Replaced the temporary web page with the responsive CPU, memory, and root
  filesystem dashboard, including threshold indicators and browser-session
  trend history.
- Replaced periodic terminal reports with a clean launcher and `h`, `t`, and
  `q` controls. CPU and RAM tests run in child processes while monitoring
  continues; the RAM sequence enforces a 1024 MiB available-memory reserve.
- Added five student-facing test procedures with safety notes and pass/fail
  criteria.
- Documented Raspberry Pi OS/Ubuntu package prerequisites and the CivetWeb
  Makefile fallback for systems without `civetweb.pc`.

- Replaced the ncurses presentation with an embedded CivetWeb server in the
  web-server milestone.
- Added static placeholder assets under `web/` and read-only JSON health and
  metrics endpoints with mutex-protected report snapshots.
- Added configurable host, port, and static root options. The default listener
  is loopback; LAN binding is opt-in.
- Removed ncurses source and build dependencies; added JSON serializer tests.
- Updated requirements, design, README, test plan, and status to match the
  implemented CPU/RAM/root-filesystem monitoring behavior.

## Earlier milestones

- Replaced the fortune-generator example with Linux equipment health reporting.
- Added temperature, memory, disk, load, and uptime collection.
- Added threshold-based health status and optional CSV logging.
- Renamed the sample data file to `temperature_log.csv`.
- Updated the requirements, design, README, test plan, and project status.
- Added a mock GPIO backend and automated tests for status thresholds, LED
  mapping, initialization failures, and cleanup.
