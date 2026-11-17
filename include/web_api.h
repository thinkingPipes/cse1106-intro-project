#ifndef WEB_API_H
#define WEB_API_H

#include <stddef.h>

#include "health_monitor.h"

#define WEB_API_HISTORY_DEFAULT_LIMIT 120U
#define WEB_API_HISTORY_MAX_LIMIT 1000U

typedef struct {
    char sampled_at[32];
    char status[16];
    bool has_memory_used_pct;
    double memory_used_pct;
    bool has_root_used_pct;
    double root_used_pct;
    unsigned int warning_threshold_pct;
    bool has_cpu_used_pct;
    double cpu_used_pct;
    unsigned int cpu_warning_threshold_pct;
} HistorySample;

/* Serialize a copied report as UTF-8 JSON. Returns bytes written, or -1 if the
 * buffer is too small or an argument is invalid. timestamp_utc may be NULL
 * when no sample has been collected. */
int web_api_health_json(const HealthReport *report, const char *timestamp_utc,
                        char *buffer, size_t buffer_size);
int web_api_metrics_json(const HealthReport *report, const char *timestamp_utc,
                         char *buffer, size_t buffer_size);
int web_api_history_json(const HistorySample *samples, size_t count,
                         char *buffer, size_t buffer_size);
int web_api_read_history(const char *path, size_t limit, HistorySample *samples,
                         size_t *count);
int web_api_parse_history_limit(const char *query_string, size_t *limit);

#endif
