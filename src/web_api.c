#include "web_api.h"

#include <stdarg.h>
#include <stdio.h>

static int appendf(char *buffer, size_t size, size_t *used,
                   const char *format, ...)
{
    if (*used >= size) {
        return -1;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + *used, size - *used, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= size - *used) {
        return -1;
    }
    *used += (size_t)written;
    return 0;
}

static int append_timestamp(char *buffer, size_t size, size_t *used,
                            const char *timestamp_utc)
{
    return timestamp_utc == NULL
               ? appendf(buffer, size, used, "null")
               : appendf(buffer, size, used, "\"%s\"", timestamp_utc);
}

int web_api_health_json(const HealthReport *report, const char *timestamp_utc,
                        char *buffer, size_t buffer_size)
{
    if (report == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }
    size_t used = 0;
    if (appendf(buffer, buffer_size, &used,
                "{\"api_version\":1,\"status\":\"%s\",\"sampled_at\":",
                health_status_name(report->status)) < 0 ||
        append_timestamp(buffer, buffer_size, &used, timestamp_utc) < 0 ||
        appendf(buffer, buffer_size, &used, ",\"ok\":true}\n") < 0) {
        return -1;
    }
    return (int)used;
}

int web_api_metrics_json(const HealthReport *report, const char *timestamp_utc,
                         char *buffer, size_t buffer_size)
{
    if (report == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }
    size_t used = 0;
    double memory_pct = report->has_memory && report->memory_total_kb > 0
                            ? 100.0 * (double)(report->memory_total_kb -
                                               report->memory_available_kb) /
                                  (double)report->memory_total_kb
                            : 0.0;
    double disk_pct = report->has_disk && report->disk_total_bytes > 0
                          ? 100.0 * (double)(report->disk_total_bytes -
                                             report->disk_available_bytes) /
                                (double)report->disk_total_bytes
                          : 0.0;
    if (appendf(buffer, buffer_size, &used,
                "{\"api_version\":1,\"status\":\"%s\",\"sampled_at\":",
                health_status_name(report->status)) < 0 ||
        append_timestamp(buffer, buffer_size, &used, timestamp_utc) < 0 ||
        appendf(buffer, buffer_size, &used, ",\"cpu\":{\"available\":%s,\"used_pct\":",
                report->has_cpu ? "true" : "false") < 0) {
        return -1;
    }
    if ((report->has_cpu
             ? appendf(buffer, buffer_size, &used, "%.1f",
                       report->cpu_used_pct)
             : appendf(buffer, buffer_size, &used, "null")) < 0 ||
        appendf(buffer, buffer_size, &used,
                ",\"warning_threshold_pct\":%u},\"memory\":{\"available\":%s,\"total_kb\":",
                report->cpu_warning_threshold_pct,
                report->has_memory ? "true" : "false") < 0) {
        return -1;
    }
    if ((report->has_memory
             ? appendf(buffer, buffer_size, &used, "%llu,\"available_kb\":%llu,\"used_pct\":%.1f",
                       (unsigned long long)report->memory_total_kb,
                       (unsigned long long)report->memory_available_kb,
                       memory_pct)
             : appendf(buffer, buffer_size, &used,
                       "null,\"available_kb\":null,\"used_pct\":null")) < 0 ||
        appendf(buffer, buffer_size, &used,
                ",\"warning_threshold_pct\":%u},\"root_filesystem\":{\"available\":%s,\"total_bytes\":",
                report->warning_threshold_pct,
                report->has_disk ? "true" : "false") < 0) {
        return -1;
    }
    if ((report->has_disk
             ? appendf(buffer, buffer_size, &used, "%llu,\"available_bytes\":%llu,\"used_pct\":%.1f",
                       (unsigned long long)report->disk_total_bytes,
                       (unsigned long long)report->disk_available_bytes,
                       disk_pct)
             : appendf(buffer, buffer_size, &used,
                       "null,\"available_bytes\":null,\"used_pct\":null")) < 0 ||
        appendf(buffer, buffer_size, &used,
                ",\"warning_threshold_pct\":%u}}\n",
                report->warning_threshold_pct) < 0) {
        return -1;
    }
    return (int)used;
}
