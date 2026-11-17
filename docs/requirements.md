# Requirements

## Purpose

Demonstrate C11 programming, Linux system monitoring, and physical GPIO
interaction on a Raspberry Pi or compatible Linux system. The monitor provides
a read-only HTTP presentation layer over the existing C monitoring engine.

## Functional requirements

- Collect CPU utilization from `/proc/stat`, memory use from `/proc/meminfo`,
  and root-filesystem use through `statvfs`.
- Preserve separate CPU (default 60%) and memory/storage (default 70%) warning
  thresholds. A value warns only when it exceeds its threshold.
- Report `HEALTHY`, `WARNING`, or `UNKNOWN`; unavailable measurements must not
  be represented as zero.
- Optionally append each sample to CSV with `--log FILE`.
- Drive GPIO 17 HIGH for `WARNING` and LOW for other statuses, and attempt to
  leave it LOW during cleanup. Initialization and update failures are reported.
- Serve static assets from a configurable web root and expose read-only
  `GET /api/health` and `GET /api/metrics` endpoints returning JSON.
- Support configurable IPv4 bind address and port. The default bind address
  is loopback. No remote administrative or GPIO-control routes are provided.
- Keep collection and threshold evaluation in the C11 monitoring engine.
  HTTP handlers must read a synchronized snapshot and must not collect metrics.
- Stop the HTTP server and release its resources cleanly on SIGINT/SIGTERM.

## Platform and dependencies

- Linux, C11 compiler, Make, and pkg-config.
- libgpiod for physical GPIO and CivetWeb for embedded HTTP/static file
  serving. ncurses is not used.
- Read access to `/proc/stat`, `/proc/meminfo`, root filesystem statistics,
  and access to the GPIO chip for the warning LED.

Temperature, swap, OS metadata, network throughput, historical analytics,
authentication, and a finished dashboard are outside this milestone.
