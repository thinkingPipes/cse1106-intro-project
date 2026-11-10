#define _POSIX_C_SOURCE 200809L

#include "health_monitor.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>

static bool read_memory(HealthReport *report)
{
    FILE *file = fopen("/proc/meminfo", "r");
    if (file == NULL) {
        return false;
    }

    char line[128];
    bool has_total = false;
    bool has_available = false;
    uint64_t total = 0;
    uint64_t available = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned long long value;
        if (sscanf(line, "MemTotal: %llu kB", &value) == 1) {
            total = (uint64_t)value;
            has_total = true;
        } else if (sscanf(line, "MemAvailable: %llu kB", &value) == 1) {
            available = (uint64_t)value;
            has_available = true;
        }
    }
    fclose(file);
    if (!has_total || !has_available || total == 0 || available > total) {
        return false;
    }
    report->memory_total_kb = total;
    report->memory_available_kb = available;
    report->has_memory = true;
    return true;
}

static bool read_disk(HealthReport *report)
{
    struct statvfs stats;
    if (statvfs("/", &stats) != 0) {
        return false;
    }
    uint64_t block_size = stats.f_frsize != 0 ? stats.f_frsize : stats.f_bsize;
    if (block_size == 0 || stats.f_blocks == 0) {
        return false;
    }
    report->disk_total_bytes = (uint64_t)stats.f_blocks * block_size;
    report->disk_available_bytes = (uint64_t)stats.f_bavail * block_size;
    if (report->disk_available_bytes > report->disk_total_bytes) {
        return false;
    }
    report->has_disk = true;
    return true;
}

static bool utilization_exceeds_threshold(uint64_t used, uint64_t total,
                                          unsigned int threshold_pct)
{
    uint64_t threshold_used = (total / 100) * threshold_pct +
                              ((total % 100) * threshold_pct) / 100;
    return used > threshold_used;
}

HealthStatus evaluate_health_status(const HealthReport *report)
{
    if (!report->has_memory && !report->has_disk) {
        return HEALTH_UNKNOWN;
    }
    if (report->has_memory &&
        utilization_exceeds_threshold(report->memory_total_kb -
                                          report->memory_available_kb,
                                      report->memory_total_kb,
                                      report->warning_threshold_pct)) {
        return HEALTH_WARNING;
    }
    if (report->has_disk &&
        utilization_exceeds_threshold(report->disk_total_bytes -
                                          report->disk_available_bytes,
                                      report->disk_total_bytes,
                                      report->warning_threshold_pct)) {
        return HEALTH_WARNING;
    }
    return HEALTHY;
}

const char *health_status_name(HealthStatus status)
{
    switch (status) {
    case HEALTHY:
        return "HEALTHY";
    case HEALTH_WARNING:
        return "WARNING";
    case HEALTH_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

void collect_health_report(HealthReport *report,
                           unsigned int warning_threshold_pct)
{
    memset(report, 0, sizeof(*report));
    report->warning_threshold_pct = warning_threshold_pct;
    read_memory(report);
    read_disk(report);
    report->status = evaluate_health_status(report);
}

void print_health_report(const HealthReport *report)
{
    puts("Raspberry Pi Equipment Health Monitor");
    puts("-------------------------------------");
    if (report->has_memory) {
        double used_percent =
            100.0 * (double)(report->memory_total_kb -
                             report->memory_available_kb) /
            (double)report->memory_total_kb;
        printf("Memory used:          %.1f%% (%llu / %llu kB total)\n",
               used_percent,
               (unsigned long long)(report->memory_total_kb - report->memory_available_kb),
               (unsigned long long)report->memory_total_kb);
    } else {
        puts("Memory used:          N/A");
    }
    if (report->has_disk) {
        double used_percent =
            100.0 * (double)(report->disk_total_bytes -
                             report->disk_available_bytes) /
            (double)report->disk_total_bytes;
        printf("Root filesystem used: %.1f%%\n", used_percent);
    } else {
        puts("Root filesystem used: N/A");
    }
    printf("Warning threshold:    %u%% memory/storage used\n",
           report->warning_threshold_pct);
    printf("Overall status:       %s\n", health_status_name(report->status));
}

static bool format_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm utc_time;
    if (now == (time_t)-1 || gmtime_r(&now, &utc_time) == NULL) {
        return false;
    }
    return strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", &utc_time) != 0;
}

int append_health_report(const char *path, const HealthReport *report)
{
    struct stat file_stats;
    bool write_header = false;
    if (stat(path, &file_stats) == 0) {
        write_header = file_stats.st_size == 0;
    } else if (errno == ENOENT) {
        write_header = true;
    } else {
        return -1;
    }
    FILE *file = fopen(path, "a");
    if (file == NULL) {
        return -1;
    }
    char timestamp[32];
    if (!format_timestamp(timestamp, sizeof(timestamp))) {
        fclose(file);
        return -1;
    }
    int result = 0;
    if (write_header &&
        fputs("timestamp_utc,memory_used_pct,root_used_pct,warning_threshold_pct,status\n",
              file) == EOF) {
        result = -1;
    }
    if (result == 0 && fprintf(file, "%s,", timestamp) < 0) {
        result = -1;
    }
    if (result == 0 && report->has_memory) {
        double used_percent =
            100.0 * (double)(report->memory_total_kb -
                             report->memory_available_kb) /
            (double)report->memory_total_kb;
        if (fprintf(file, "%.1f", used_percent) < 0) {
            result = -1;
        }
    }
    if (result == 0 && fputc(',', file) == EOF) {
        result = -1;
    }
    if (result == 0 && report->has_disk) {
        double used_percent =
            100.0 * (double)(report->disk_total_bytes -
                             report->disk_available_bytes) /
            (double)report->disk_total_bytes;
        if (fprintf(file, "%.1f", used_percent) < 0) {
            result = -1;
        }
    }
    if (result == 0 &&
        fprintf(file, ",%u,%s\n", report->warning_threshold_pct,
                health_status_name(report->status)) < 0) {
        result = -1;
    }
    if (fclose(file) != 0) {
        result = -1;
    }
    return result;
}
