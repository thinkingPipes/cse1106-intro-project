#ifndef WEB_API_H
#define WEB_API_H

#include <stddef.h>

#include "health_monitor.h"

/* Serialize a copied report as UTF-8 JSON. Returns bytes written, or -1 if the
 * buffer is too small or an argument is invalid. timestamp_utc may be NULL
 * when no sample has been collected. */
int web_api_health_json(const HealthReport *report, const char *timestamp_utc,
                        char *buffer, size_t buffer_size);
int web_api_metrics_json(const HealthReport *report, const char *timestamp_utc,
                         char *buffer, size_t buffer_size);

#endif
