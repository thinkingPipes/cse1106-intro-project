/* Guard keeps shared report declarations usable from multiple source files. */
#ifndef HEALTH_MONITOR_H
#define HEALTH_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

/* Ordered severities let the evaluator retain the most serious condition. */
typedef enum {
    HEALTH_UNKNOWN,
    HEALTHY,
    HEALTH_WARNING,
    HEALTH_CRITICAL
} HealthStatus;

/* One snapshot passed from the Linux collector to output and GPIO modules.
 * Each has_* flag distinguishes a missing system source from a real zero. */
typedef struct {
    bool has_temperature;
    double temperature_c;
    bool has_load;
    double load_1m;
    bool has_memory;
    uint64_t memory_total_kb;
    uint64_t memory_available_kb;
    bool has_disk;
    uint64_t disk_total_bytes;
    uint64_t disk_available_bytes;
    bool has_uptime;
    double uptime_seconds;
    unsigned int warning_threshold_pct;
    HealthStatus status;
} HealthReport;

/* Public collector/output interface: gather, evaluate, print, and optionally log. */
void collect_health_report(HealthReport *report, unsigned int warning_threshold_pct);
HealthStatus evaluate_health_status(const HealthReport *report);
void print_health_report(const HealthReport *report);
int append_health_report(const char *path, const HealthReport *report);
const char *health_status_name(HealthStatus status);

#endif
