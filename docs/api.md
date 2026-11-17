# HTTP API Contract

API version 1 is read-only. The server serves these routes over HTTP; both
successful endpoints accept `GET`, return `application/json; charset=utf-8`,
and return the latest completed monitoring sample. A request handler copies a
mutex-protected report snapshot and does not collect metrics.

The `api_version` field is the integer `1`. Field names, types, null rules, and
units below describe the JSON emitted by `src/web_api.c`.

## `GET /api/health`

HTTP 200 means the monitor service responded; it does not mean the monitored
machine is healthy. The response fields are:

| Field | JSON type | Values / meaning |
|---|---|---|
| `api_version` | integer | Always `1`. |
| `status` | string | `HEALTHY`, `WARNING`, or `UNKNOWN`. Overall monitored-system status. |
| `sampled_at` | string or null | UTC time of the latest report, formatted `YYYY-MM-DDTHH:MM:SSZ`; null if no sample timestamp is available. |
| `ok` | boolean | Always `true` in this response. It indicates the endpoint responded, independently of `status`. |

Example:

```json
{"api_version":1,"status":"HEALTHY","sampled_at":"2026-11-17T19:29:04Z","ok":true}
```

Before the first report, `sampled_at` is `null` and `status` is `UNKNOWN`. If
timestamp generation fails for a later report, `sampled_at` remains `null`
while `status` still reflects that report.

## `GET /api/metrics`

The response contains the same top-level fields `api_version`, `status`, and
`sampled_at` as `/api/health`. It does not include `ok`. It also contains:

| Field | JSON type | Values / meaning |
|---|---|---|
| `cpu` | object | Aggregate CPU utilization. |
| `cpu.available` | boolean | True when `/proc/stat` counter deltas produced a CPU sample. |
| `cpu.used_pct` | number or null | CPU utilization in percent, rounded to one decimal place; null when `cpu.available` is false. |
| `cpu.warning_threshold_pct` | integer | CPU warning threshold in percent; included even when CPU is unavailable. |
| `memory` | object | Memory counters and calculated utilization. |
| `memory.available` | boolean | True when valid `MemTotal` and `MemAvailable` values were read. |
| `memory.total_kb` | integer or null | Total memory in the `kB` units reported by `/proc/meminfo`; null when unavailable. |
| `memory.available_kb` | integer or null | Available memory in the same `kB` units; null when unavailable. |
| `memory.used_pct` | number or null | `(total_kb - available_kb) / total_kb * 100`, rounded to one decimal place; null when unavailable. |
| `memory.warning_threshold_pct` | integer | Memory/storage warning threshold in percent; included even when memory is unavailable. |
| `root_filesystem` | object | Root filesystem counters and calculated utilization. |
| `root_filesystem.available` | boolean | True when valid root filesystem capacity values were read. |
| `root_filesystem.total_bytes` | integer or null | Total root filesystem capacity in bytes; null when unavailable. |
| `root_filesystem.available_bytes` | integer or null | Available root filesystem capacity in bytes (`f_bavail`); null when unavailable. |
| `root_filesystem.used_pct` | number or null | `(total_bytes - available_bytes) / total_bytes * 100`, rounded to one decimal place; null when unavailable. |
| `root_filesystem.warning_threshold_pct` | integer | Memory/storage warning threshold in percent; included even when root filesystem data is unavailable. |

Every metric object always includes `available` and its threshold. If a metric
is unavailable, its measured values are `null`, never numeric zero. If it is
available, its measured values are JSON numbers. Thresholds are integers from
1 through 100 and are always included. Percentages are JSON numbers in the
0–100 range rounded to one decimal place. `status` has the same three possible
strings as `/api/health`.

Example captured from the version 1 serializer:

```json
{"api_version":1,"status":"HEALTHY","sampled_at":"2026-11-17T19:29:04Z","cpu":{"available":true,"used_pct":14.9,"warning_threshold_pct":60},"memory":{"available":true,"total_kb":16293108,"available_kb":13380320,"used_pct":17.9,"warning_threshold_pct":70},"root_filesystem":{"available":true,"total_bytes":161061273600,"available_bytes":142517116928,"used_pct":11.5,"warning_threshold_pct":70}}
```

Example shape when metric sources are unavailable:

```json
{"api_version":1,"status":"UNKNOWN","sampled_at":null,"cpu":{"available":false,"used_pct":null,"warning_threshold_pct":60},"memory":{"available":false,"total_kb":null,"available_kb":null,"used_pct":null,"warning_threshold_pct":70},"root_filesystem":{"available":false,"total_bytes":null,"available_bytes":null,"used_pct":null,"warning_threshold_pct":70}}
```

## `GET /api/history`

Returns up to `limit` most recent valid CSV samples in chronological order.
`limit` defaults to 120 and must be an unsigned decimal integer from 1 to
1000. Invalid, repeated, or additional query parameters return HTTP 400.
When no log path is configured, the CSV file is missing, or it has no valid
records, the endpoint returns HTTP 200 with an empty `samples` array. Malformed
CSV rows are skipped. CSV reads and writes share a process mutex.

The response fields are:

| Field | JSON type | Meaning |
|---|---|---|
| `api_version` | integer | Always `1`. |
| `samples` | array | Most recent valid records, oldest first. |
| `samples[].sampled_at` | string | UTC CSV timestamp, `YYYY-MM-DDTHH:MM:SSZ`. |
| `samples[].status` | string | `HEALTHY`, `WARNING`, or `UNKNOWN`. |
| `samples[].memory_used_pct` | number or null | Memory utilization percent, one decimal; null for unavailable measurement. |
| `samples[].root_used_pct` | number or null | Root filesystem utilization percent, one decimal; null when unavailable. |
| `samples[].warning_threshold_pct` | integer | Memory/storage warning threshold percent. |
| `samples[].cpu_used_pct` | number or null | CPU utilization percent, one decimal; null when unavailable. |
| `samples[].cpu_warning_threshold_pct` | integer | CPU warning threshold percent. |

Example:

```json
{"api_version":1,"samples":[{"sampled_at":"2026-11-17T19:28:04Z","status":"HEALTHY","memory_used_pct":17.9,"root_used_pct":11.5,"warning_threshold_pct":70,"cpu_used_pct":14.9,"cpu_warning_threshold_pct":60},{"sampled_at":"2026-11-17T19:29:04Z","status":"UNKNOWN","memory_used_pct":null,"root_used_pct":11.5,"warning_threshold_pct":70,"cpu_used_pct":null,"cpu_warning_threshold_pct":60}]}
```

## API errors

API requests using a method other than GET return HTTP 405 and JSON with
`Content-Type: application/json; charset=utf-8`. A path that matches an API
handler prefix but is not the exact route, such as `/api/health/extra`, returns
HTTP 404 in the same error shape. Error fields are both strings:

```json
{"error":"method_not_allowed","message":"Only GET is supported"}
```

```json
{"error":"not_found","message":"API endpoint not found"}
```

If JSON serialization fails, the handler returns HTTP 500 with:

```json
{"error":"serialization_failed","message":"Could not serialize health report"}
```
