#define _POSIX_C_SOURCE 200809L

#include "health_monitor.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>

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

static HealthStatus evaluate_status(const HealthReport *report)
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
        double available_percent =
            100.0 * (double)report->memory_available_kb /
            (double)report->memory_total_kb;
        if (available_percent < 5.0) {
            status = HEALTH_CRITICAL;
        } else if (available_percent < 15.0 && status < HEALTH_WARNING) {
            status = HEALTH_WARNING;
        }
    }

    if (report->has_disk) {
        has_health_metric = true;
        double available_percent =
            100.0 * (double)report->disk_available_bytes /
            (double)report->disk_total_bytes;
        if (available_percent < 5.0) {
            status = HEALTH_CRITICAL;
        } else if (available_percent < 15.0 && status < HEALTH_WARNING) {
            status = HEALTH_WARNING;
        }
    }

    return has_health_metric ? status : HEALTH_UNKNOWN;
}

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

void collect_health_report(HealthReport *report)
{
    memset(report, 0, sizeof(*report));
    read_temperature(report);
    read_load(report);
    read_memory(report);
    read_disk(report);
    read_uptime(report);
    report->status = evaluate_status(report);
}

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
    puts("Raspberry Pi Equipment Health Monitor");
    puts("-------------------------------------");

    if (report->has_temperature) {
        printf("CPU temperature:      %.1f C\n", report->temperature_c);
    } else {
        puts("CPU temperature:      N/A");
    }

    if (report->has_memory) {
        double available_percent =
            100.0 * (double)report->memory_available_kb /
            (double)report->memory_total_kb;
        printf("Memory available:     %.1f%% (%llu / %llu kB)\n",
               available_percent,
               (unsigned long long)report->memory_available_kb,
               (unsigned long long)report->memory_total_kb);
    } else {
        puts("Memory available:     N/A");
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
        fputs("timestamp_utc,temperature_c,load_1m,memory_available_pct,"
              "root_used_pct,status\n", file) == EOF) {
        result = -1;
    }

    if (result == 0 &&
        fprintf(file, "%s,", timestamp) < 0) {
        result = -1;
    }
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
            double available_percent =
                100.0 * (double)report->memory_available_kb /
                (double)report->memory_total_kb;
            result = fprintf(file, "%.1f", available_percent) < 0 ? -1 : 0;
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
            fprintf(file, ",%s\n", health_status_name(report->status)) < 0) {
            result = -1;
        }
    }

    if (fclose(file) != 0) {
        result = -1;
    }
    return result;
}
