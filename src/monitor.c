#define _POSIX_C_SOURCE 200809L

#include "health_monitor.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>

#define HEALTH_CSV_HEADER \
    "timestamp_utc,memory_used_pct,root_used_pct,warning_threshold_pct," \
    "cpu_used_pct,cpu_warning_threshold_pct,status"

bool parse_cpu_stat_line(const char *line, uint64_t *total_ticks,
                         uint64_t *idle_ticks)
{
    if (line == NULL || total_ticks == NULL || idle_ticks == NULL ||
        strncmp(line, "cpu", 3) != 0 || !isspace((unsigned char)line[3])) {
        return false;
    }

    unsigned long long counters[10] = {0};
    size_t count = 0;
    const char *cursor = line + 3;
    while (*cursor != '\0' && count < 10) {
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        if (!isdigit((unsigned char)*cursor)) {
            return false;
        }
        errno = 0;
        char *end = NULL;
        unsigned long long value = strtoull(cursor, &end, 10);
        if (errno == ERANGE || end == cursor ||
            (*end != '\0' && !isspace((unsigned char)*end))) {
            return false;
        }
        counters[count++] = value;
        cursor = end;
    }
    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    if (*cursor != '\0' || count < 4) {
        return false;
    }

    uint64_t total = 0;
    size_t total_fields = count < 8 ? count : 8;
    for (size_t i = 0; i < total_fields; ++i) {
        uint64_t value = (uint64_t)counters[i];
        if (UINT64_MAX - total < value) {
            return false;
        }
        total += value;
    }
    uint64_t idle = (uint64_t)counters[3];
    if (count >= 5) {
        uint64_t iowait = (uint64_t)counters[4];
        if (UINT64_MAX - idle < iowait) {
            return false;
        }
        idle += iowait;
    }
    if (total == 0 || idle > total) {
        return false;
    }
    *total_ticks = total;
    *idle_ticks = idle;
    return true;
}

bool update_cpu_utilization(CpuSampleState *state, uint64_t total_ticks,
                            uint64_t idle_ticks, double *utilization_pct)
{
    if (state == NULL || utilization_pct == NULL || total_ticks == 0 ||
        idle_ticks > total_ticks) {
        return false;
    }
    if (!state->has_previous) {
        state->previous_total_ticks = total_ticks;
        state->previous_idle_ticks = idle_ticks;
        state->has_previous = true;
        return false;
    }
    if (total_ticks <= state->previous_total_ticks ||
        idle_ticks < state->previous_idle_ticks) {
        state->previous_total_ticks = total_ticks;
        state->previous_idle_ticks = idle_ticks;
        return false;
    }

    uint64_t total_delta = total_ticks - state->previous_total_ticks;
    uint64_t idle_delta = idle_ticks - state->previous_idle_ticks;
    state->previous_total_ticks = total_ticks;
    state->previous_idle_ticks = idle_ticks;
    if (total_delta == 0 || idle_delta > total_delta) {
        return false;
    }
    *utilization_pct =
        100.0 * (double)(total_delta - idle_delta) / (double)total_delta;
    return true;
}

static bool read_cpu_utilization(CpuSampleState *state,
                                 double *utilization_pct)
{
    FILE *file = fopen("/proc/stat", "r");
    if (file == NULL) {
        return false;
    }
    char line[256];
    bool parsed = fgets(line, sizeof(line), file) != NULL;
    fclose(file);
    uint64_t total_ticks;
    uint64_t idle_ticks;
    if (!parsed || !parse_cpu_stat_line(line, &total_ticks, &idle_ticks)) {
        return false;
    }
    return update_cpu_utilization(state, total_ticks, idle_ticks,
                                  utilization_pct);
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

static bool utilization_exceeds_threshold(uint64_t used, uint64_t total,
                                          unsigned int threshold_pct)
{
    uint64_t threshold_used = (total / 100) * threshold_pct +
                              ((total % 100) * threshold_pct) / 100;
    return used > threshold_used;
}

HealthStatus evaluate_health_status(const HealthReport *report)
{
    if (!report->has_cpu && !report->has_memory && !report->has_disk) {
        return HEALTH_UNKNOWN;
    }
    if (report->has_cpu &&
        report->cpu_used_pct > (double)report->cpu_warning_threshold_pct) {
        return HEALTH_WARNING;
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
                           unsigned int warning_threshold_pct,
                           unsigned int cpu_warning_threshold_pct,
                           CpuSampleState *cpu_state)
{
    memset(report, 0, sizeof(*report));
    report->warning_threshold_pct = warning_threshold_pct;
    report->cpu_warning_threshold_pct = cpu_warning_threshold_pct;
    if (cpu_state != NULL) {
        report->has_cpu = read_cpu_utilization(cpu_state,
                                               &report->cpu_used_pct);
    }
    read_memory(report);
    read_disk(report);
    report->status = evaluate_health_status(report);
}

void print_health_report(const HealthReport *report)
{
    puts("Raspberry Pi Equipment Health Monitor");
    puts("-------------------------------------");
    if (report->has_cpu) {
        printf("CPU utilization:      %.1f%%\n", report->cpu_used_pct);
    } else {
        puts("CPU utilization:      N/A (waiting for two valid samples)");
    }
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
    printf("RAM/storage threshold:%u%% used\n",
           report->warning_threshold_pct);
    printf("CPU warning threshold:%u%% used\n",
           report->cpu_warning_threshold_pct);
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
    if (!write_header) {
        FILE *existing = fopen(path, "r");
        if (existing == NULL) {
            return -1;
        }
        char existing_header[256];
        bool header_read = fgets(existing_header, sizeof(existing_header),
                                 existing) != NULL;
        fclose(existing);
        if (!header_read) {
            errno = EINVAL;
            return -1;
        }
        existing_header[strcspn(existing_header, "\r\n")] = '\0';
        if (strcmp(existing_header, HEALTH_CSV_HEADER) != 0) {
            errno = EINVAL;
            return -1;
        }
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
        fprintf(file, "%s\n", HEALTH_CSV_HEADER) < 0) {
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
    if (result == 0 && fprintf(file, ",%u,", report->warning_threshold_pct) < 0) {
        result = -1;
    }
    if (result == 0 && report->has_cpu &&
        fprintf(file, "%.1f", report->cpu_used_pct) < 0) {
        result = -1;
    }
    if (result == 0 &&
        fprintf(file, ",%u,%s\n", report->cpu_warning_threshold_pct,
                health_status_name(report->status)) < 0) {
        result = -1;
    }
    if (fclose(file) != 0) {
        result = -1;
    }
    return result;
}
