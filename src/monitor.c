#define _POSIX_C_SOURCE 200809L

#include "health_monitor.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>

/* Read Linux thermal sysfs, whose standard value is milli-degrees Celsius. */
static bool read_temperature(HealthReport *report)
{
    FILE *file = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (file == NULL) {
        return false;
    }

    long milli_degrees;
    bool valid = fscanf(file, "%ld", &milli_degrees) == 1;
    fclose(file);
    if (!valid) {
        return false;
    }

    report->temperature_c = (double)milli_degrees / 1000.0;
    report->has_temperature = true;
    return true;
}

/* /proc/loadavg starts with the one-minute load average used by this report. */
static bool read_load(HealthReport *report)
{
    FILE *file = fopen("/proc/loadavg", "r");
    if (file == NULL) {
        return false;
    }

    bool valid = fscanf(file, "%lf", &report->load_1m) == 1;
    fclose(file);
    report->has_load = valid;
    return valid;
}

/* MemAvailable estimates memory usable without swapping; retain total and
 * available values so display and threshold logic share the same snapshot. */
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

/* statvfs reports filesystem blocks; f_bavail excludes space unavailable to
 * ordinary users, making it a useful measure of space available to the app. */
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

/* The first /proc/uptime field is elapsed seconds since boot. */
static bool read_uptime(HealthReport *report)
{
    FILE *file = fopen("/proc/uptime", "r");
    if (file == NULL) {
        return false;
    }

    bool valid = fscanf(file, "%lf", &report->uptime_seconds) == 1;
    fclose(file);
    report->has_uptime = valid;
    return valid;
}

/* Compare against a floored integer percentage without multiplying the full
 * total by 100, which could overflow for large counters. Equality is healthy. */
static bool utilization_exceeds_threshold(uint64_t used, uint64_t total,
                                          unsigned int threshold_pct)
{
    uint64_t threshold_used =
        (total / 100) * threshold_pct +
        ((total % 100) * threshold_pct) / 100;
    return used > threshold_used;
}

/* Combine only available health metrics, preserving the highest severity.
 * Load and uptime are informational and do not affect status. */
HealthStatus evaluate_health_status(const HealthReport *report)
{
    bool has_health_metric = false;
    HealthStatus status = HEALTHY;

    if (report->has_temperature) {
        has_health_metric = true;
        if (report->temperature_c >= 80.0) {
            status = HEALTH_CRITICAL;
        } else if (report->temperature_c >= 70.0 && status < HEALTH_WARNING) {
            status = HEALTH_WARNING;
        }
    }

    if (report->has_memory) {
        has_health_metric = true;
        uint64_t used_kb = report->memory_total_kb -
                           report->memory_available_kb;
        if (utilization_exceeds_threshold(used_kb,
                                          report->memory_total_kb,
                                          report->warning_threshold_pct) &&
            status < HEALTH_WARNING) {
            status = HEALTH_WARNING;
        }
    }

    if (report->has_disk) {
        has_health_metric = true;
        uint64_t used_bytes = report->disk_total_bytes -
                              report->disk_available_bytes;
        if (utilization_exceeds_threshold(used_bytes,
                                          report->disk_total_bytes,
                                          report->warning_threshold_pct) &&
            status < HEALTH_WARNING) {
            status = HEALTH_WARNING;
        }
    }

    return has_health_metric ? status : HEALTH_UNKNOWN;
}

/* Centralized names keep console and CSV status labels consistent. */
const char *health_status_name(HealthStatus status)
{
    switch (status) {
    case HEALTHY:
        return "HEALTHY";
    case HEALTH_WARNING:
        return "WARNING";
    case HEALTH_CRITICAL:
        return "CRITICAL";
    case HEALTH_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

void collect_health_report(HealthReport *report,
                           unsigned int warning_threshold_pct)
{
    /* Clear flags and values so failed individual reads remain explicitly absent;
     * one unavailable source does not stop collection of the others. */
    memset(report, 0, sizeof(*report));
    report->warning_threshold_pct = warning_threshold_pct;
    read_temperature(report);
    read_load(report);
    read_memory(report);
    read_disk(report);
    read_uptime(report);
    report->status = evaluate_health_status(report);
}

/* Convert elapsed seconds to a compact day/hour/minute display. */
static void print_uptime(double seconds)
{
    uint64_t total_seconds = (uint64_t)seconds;
    uint64_t days = total_seconds / 86400;
    uint64_t hours = (total_seconds % 86400) / 3600;
    uint64_t minutes = (total_seconds % 3600) / 60;

    if (days > 0) {
        printf("%llu days, %llu hours, %llu minutes",
               (unsigned long long)days,
               (unsigned long long)hours,
               (unsigned long long)minutes);
    } else {
        printf("%llu hours, %llu minutes",
               (unsigned long long)hours,
               (unsigned long long)minutes);
    }
}

void print_health_report(const HealthReport *report)
{
    /* Presence flags control N/A output; utilization is derived from used =
     * total minus available, matching the evaluator's resource calculations. */
    puts("Raspberry Pi Equipment Health Monitor");
    puts("-------------------------------------");

    if (report->has_temperature) {
        printf("CPU temperature:      %.1f C\n", report->temperature_c);
    } else {
        puts("CPU temperature:      N/A");
    }

    if (report->has_memory) {
        double used_percent =
            100.0 * (double)(report->memory_total_kb -
                             report->memory_available_kb) /
            (double)report->memory_total_kb;
        printf("Memory used:          %.1f%% (%llu / %llu kB total)\n",
               used_percent,
               (unsigned long long)report->memory_available_kb,
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

    if (report->has_load) {
        printf("System load (1 min):  %.2f\n", report->load_1m);
    } else {
        puts("System load (1 min):  N/A");
    }

    printf("System uptime:        ");
    if (report->has_uptime) {
        print_uptime(report->uptime_seconds);
        putchar('\n');
    } else {
        puts("N/A");
    }

    printf("Warning threshold:    %u%% memory/storage used\n",
           report->warning_threshold_pct);
    printf("Overall status:       %s\n", health_status_name(report->status));
}

/* CSV timestamps are UTC so log rows compare consistently across time zones. */
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
    /* Create the header for a new or empty file, then append one row. Every
     * failed write/close is propagated so callers can return a nonzero status. */
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
        fputs("timestamp_utc,temperature_c,load_1m,memory_used_pct,"
              "root_used_pct,warning_threshold_pct,status\n", file) == EOF) {
        result = -1;
    }

    if (result == 0 &&
        fprintf(file, "%s,", timestamp) < 0) {
        result = -1;
    }
    /* Missing metrics are emitted as empty fields, preserving CSV column order
     * without incorrectly representing unavailable readings as zero. */
    if (result == 0) {
        if (report->has_temperature) {
            result = fprintf(file, "%.1f", report->temperature_c) < 0 ? -1 : 0;
        }
        if (result == 0 && fputc(',', file) == EOF) {
            result = -1;
        }
    }
    if (result == 0) {
        if (report->has_load) {
            result = fprintf(file, "%.2f", report->load_1m) < 0 ? -1 : 0;
        }
        if (result == 0 && fputc(',', file) == EOF) {
            result = -1;
        }
    }
    if (result == 0) {
        if (report->has_memory) {
            double used_percent =
                100.0 * (double)(report->memory_total_kb -
                                 report->memory_available_kb) /
                (double)report->memory_total_kb;
            result = fprintf(file, "%.1f", used_percent) < 0 ? -1 : 0;
        }
        if (result == 0 && fputc(',', file) == EOF) {
            result = -1;
        }
    }
    if (result == 0) {
        if (report->has_disk) {
            double used_percent =
                100.0 * (double)(report->disk_total_bytes -
                                 report->disk_available_bytes) /
                (double)report->disk_total_bytes;
            result = fprintf(file, "%.1f", used_percent) < 0 ? -1 : 0;
        }
        if (result == 0 &&
            fprintf(file, ",%u,%s\n", report->warning_threshold_pct,
                    health_status_name(report->status)) < 0) {
            result = -1;
        }
    }

    if (fclose(file) != 0) {
        result = -1;
    }
    return result;
}
