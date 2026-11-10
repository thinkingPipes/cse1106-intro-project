#ifndef HEALTH_MONITOR_H
#define HEALTH_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HEALTH_UNKNOWN,
    HEALTHY,
    HEALTH_WARNING
} HealthStatus;

typedef struct {
    uint64_t previous_total_ticks;
    uint64_t previous_idle_ticks;
    bool has_previous;
} CpuSampleState;

typedef struct {
    bool has_cpu;
    double cpu_used_pct;
    unsigned int cpu_warning_threshold_pct;
    bool has_memory;
    uint64_t memory_total_kb;
    uint64_t memory_available_kb;
    bool has_disk;
    uint64_t disk_total_bytes;
    uint64_t disk_available_bytes;
    unsigned int warning_threshold_pct;
    HealthStatus status;
} HealthReport;

void collect_health_report(HealthReport *report,
                           unsigned int warning_threshold_pct,
                           unsigned int cpu_warning_threshold_pct,
                           CpuSampleState *cpu_state);
bool parse_cpu_stat_line(const char *line, uint64_t *total_ticks,
                         uint64_t *idle_ticks);
bool update_cpu_utilization(CpuSampleState *state, uint64_t total_ticks,
                            uint64_t idle_ticks, double *utilization_pct);
HealthStatus evaluate_health_status(const HealthReport *report);
void print_health_report(const HealthReport *report);
int append_health_report(const char *path, const HealthReport *report);
const char *health_status_name(HealthStatus status);

#endif
