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
    bool has_memory;
    uint64_t memory_total_kb;
    uint64_t memory_available_kb;
    bool has_disk;
    uint64_t disk_total_bytes;
    uint64_t disk_available_bytes;
    unsigned int warning_threshold_pct;
    HealthStatus status;
} HealthReport;

void collect_health_report(HealthReport *report, unsigned int warning_threshold_pct);
HealthStatus evaluate_health_status(const HealthReport *report);
void print_health_report(const HealthReport *report);
int append_health_report(const char *path, const HealthReport *report);
const char *health_status_name(HealthStatus status);

#endif
