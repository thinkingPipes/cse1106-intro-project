#define _POSIX_C_SOURCE 200809L
#include "web_api.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define HISTORY_CSV_HEADER "timestamp_utc,memory_used_pct,root_used_pct,warning_threshold_pct,cpu_used_pct,cpu_warning_threshold_pct,status"

int web_api_parse_history_limit(const char *query, size_t *limit)
{
    if (limit == NULL) return -1;
    *limit = WEB_API_HISTORY_DEFAULT_LIMIT;
    if (query == NULL || query[0] == '\0') return 0;
    if (strncmp(query, "limit=", 6) != 0 || strchr(query, '&') != NULL) return -1;
    const char *value = query + 6;
    if (*value == '\0') return -1;
    size_t parsed = 0;
    for (const char *p = value; *p; ++p) {
        if (*p < '0' || *p > '9' || parsed > WEB_API_HISTORY_MAX_LIMIT) return -1;
        parsed = parsed * 10U + (size_t)(*p - '0');
    }
    if (parsed == 0 || parsed > WEB_API_HISTORY_MAX_LIMIT) return -1;
    *limit = parsed;
    return 0;
}

static bool parse_pct(const char *value, bool *available, double *number)
{
    if (*value == '\0') { *available = false; return true; }
    errno = 0;
    char *end = NULL;
    double parsed = strtod(value, &end);
    if (errno != 0 || end == value || *end != '\0' || !isfinite(parsed) || parsed < 0.0 || parsed > 100.0) return false;
    *available = true; *number = parsed; return true;
}

static bool parse_uint(const char *value, unsigned int *number)
{
    if (*value == '\0') return false;
    errno = 0; char *end = NULL; unsigned long parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed > 100U) return false;
    *number = (unsigned int)parsed; return true;
}

static bool valid_timestamp(const char *s)
{
    if (strlen(s) != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z') return false;
    for (size_t i = 0; i < 20; ++i) if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 && i != 19 && (s[i] < '0' || s[i] > '9')) return false;
    return true;
}

static bool parse_row(char *line, HistorySample *sample)
{
    char *fields[7]; size_t n = 0; fields[n++] = line;
    for (char *p = line; *p; ++p) if (*p == ',') { *p = '\0'; if (n == 7) return false; fields[n++] = p + 1; }
    if (n != 7 || !valid_timestamp(fields[0]) || !parse_pct(fields[1], &sample->has_memory_used_pct, &sample->memory_used_pct) || !parse_pct(fields[2], &sample->has_root_used_pct, &sample->root_used_pct) || !parse_uint(fields[3], &sample->warning_threshold_pct) || !parse_pct(fields[4], &sample->has_cpu_used_pct, &sample->cpu_used_pct) || !parse_uint(fields[5], &sample->cpu_warning_threshold_pct) || (strcmp(fields[6], "HEALTHY") && strcmp(fields[6], "WARNING") && strcmp(fields[6], "UNKNOWN"))) return false;
    strcpy(sample->sampled_at, fields[0]); strcpy(sample->status, fields[6]); return true;
}

int web_api_read_history(const char *path, size_t limit, HistorySample *samples, size_t *count)
{
    if (count == NULL || samples == NULL || limit == 0 || limit > WEB_API_HISTORY_MAX_LIMIT) return -1;
    *count = 0; if (path == NULL || path[0] == '\0') return 0;
    (void)pthread_mutex_lock(&health_csv_mutex);
    struct stat st;
    if (stat(path, &st) != 0) { int e = errno; (void)pthread_mutex_unlock(&health_csv_mutex); if (e == ENOENT) return 0; errno = e; return -1; }
    FILE *f = fopen(path, "r");
    if (f == NULL) { (void)pthread_mutex_unlock(&health_csv_mutex); return -1; }
    char line[512]; HistorySample ring[WEB_API_HISTORY_MAX_LIMIT]; size_t used = 0, next = 0; bool header = false;
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && line[len - 1] != '\n') { int ch; while ((ch = fgetc(f)) != '\n' && ch != EOF) {} continue; }
        line[strcspn(line, "\r\n")] = '\0';
        if (!header) {
            header = true;
            if (strcmp(line, HISTORY_CSV_HEADER) != 0) {
                fclose(f); (void)pthread_mutex_unlock(&health_csv_mutex);
                errno = EINVAL; return -1;
            }
            continue;
        }
        HistorySample item = {0};
        if (!parse_row(line, &item)) continue;
        ring[next] = item; next = (next + 1) % limit; if (used < limit) ++used;
    }
    bool failed = ferror(f) != 0; fclose(f); (void)pthread_mutex_unlock(&health_csv_mutex);
    if (failed) return -1;
    size_t start = used == limit ? next : 0;
    for (size_t i = 0; i < used; ++i) samples[i] = ring[(start + i) % limit];
    *count = used; return 0;
}

int web_api_history_json(const HistorySample *samples, size_t count, char *buffer, size_t size)
{
    if ((samples == NULL && count != 0) || buffer == NULL || size == 0) return -1;
    size_t used = 0;
#define ADD(...) do { if (used >= size) return -1; int n = snprintf(buffer + used, size - used, __VA_ARGS__); if (n < 0 || (size_t)n >= size - used) return -1; used += (size_t)n; } while (0)
    ADD("{\"api_version\":1,\"samples\":[");
    for (size_t i = 0; i < count; ++i) {
        const HistorySample *s = &samples[i]; if (i) ADD(",");
        ADD("{\"sampled_at\":\"%s\",\"status\":\"%s\",\"memory_used_pct\":", s->sampled_at, s->status);
        if (s->has_memory_used_pct) ADD("%.1f", s->memory_used_pct); else ADD("null");
        ADD(",\"root_used_pct\":"); if (s->has_root_used_pct) ADD("%.1f", s->root_used_pct); else ADD("null");
        ADD(",\"warning_threshold_pct\":%u,\"cpu_used_pct\":", s->warning_threshold_pct);
        if (s->has_cpu_used_pct) ADD("%.1f", s->cpu_used_pct); else ADD("null");
        ADD(",\"cpu_warning_threshold_pct\":%u}", s->cpu_warning_threshold_pct);
    }
    ADD("]}\n"); return (int)used;
#undef ADD
}
