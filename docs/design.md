# Design

The application is a C11 sampling process with an embedded CivetWeb HTTP
server. `src/monitor.c` remains the sole metric collector and status evaluator;
it reads CPU counters from `/proc/stat`, memory from `/proc/meminfo`, and root
filesystem capacity through `statvfs`. `src/gpio_indicator.c` owns GPIO 17
initialization, output, and cleanup. The sampling loop applies each report to
the LED and optionally appends it to CSV.

`src/web_server.c` starts CivetWeb with a static document root and registers
`GET /api/health` and `GET /api/metrics`. CivetWeb worker threads can run
handlers concurrently with the sampling loop. The latest `HealthReport` and
UTC sample timestamp are copied under a pthread mutex; handlers release the
mutex before JSON formatting or network writes. Collection and GPIO access
remain on the main thread. Shutdown stops CivetWeb and joins its workers before
destroying the snapshot mutex.

`src/web_api.c` owns the API JSON representation. Percentages are calculated
from existing counters and capacities, and unavailable measurements serialize
as `null` with `available: false`. `/api/health` is a service liveness route:
HTTP 200 means the service responded, while its JSON `status` gives the
monitored system status. API routes accept GET only. CivetWeb serves static
files from `web/`; the placeholder frontend is not a finished dashboard.

CivetWeb is selected because its C API provides embedded startup, request
handlers, and static files under an MIT license. It runs worker threads, which
is why report snapshots use a mutex. GNU libmicrohttpd is a capable C
alternative with more response-assembly work and LGPL licensing. Mongoose is
capable and compact, but its current GPLv2/commercial licensing is a less
direct fit for this project. The build uses the system CivetWeb development
package through pkg-config rather than vendoring a second server source tree.

The default listener is `127.0.0.1:8080`. `--host` accepts an IPv4 address;
`--host 0.0.0.0` allows IPv4 access from the local network. The HTTP API is read-only and has no authentication;
users should bind only on a trusted network and restrict access with their
firewall. Server host, port, and static root are configurable at startup.
