#include "web_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned int tests_run;
static unsigned int tests_failed;

#define CHECK(condition, message) do { \
    ++tests_run; \
    if (condition) { \
        printf("[PASS] %s\n", message); \
    } else { \
        fprintf(stderr, "[FAIL] %s\n", message); \
        ++tests_failed; \
    } \
} while (0)

static void test_health_payload(void)
{
    HealthReport report = {0};
    report.status = HEALTH_WARNING;
    char json[256];
    int length = web_api_health_json(&report, "2026-11-17T12:00:00Z", json,
                                     sizeof(json));
    CHECK(length > 0 &&
              strcmp(json,
                     "{\"api_version\":1,\"status\":\"WARNING\",\"sampled_at\":\"2026-11-17T12:00:00Z\",\"ok\":true}\n") == 0,
          "health endpoint payload reports status and UTC sample time");
}

static void test_metrics_payload(void)
{
    HealthReport report = {0};
    report.status = HEALTHY;
    report.has_cpu = true;
    report.cpu_used_pct = 25.25;
    report.cpu_warning_threshold_pct = 60;
    report.has_memory = true;
    report.memory_total_kb = 1000;
    report.memory_available_kb = 250;
    report.warning_threshold_pct = 70;
    report.has_disk = true;
    report.disk_total_bytes = 2000;
    report.disk_available_bytes = 500;
    char json[1024];
    int length = web_api_metrics_json(&report, "2026-11-17T12:00:00Z", json,
                                      sizeof(json));
    CHECK(length > 0 && strstr(json, "\"cpu\":{\"available\":true,\"used_pct\":25.2") != NULL &&
              strstr(json, "\"memory\":{\"available\":true,\"total_kb\":1000,\"available_kb\":250,\"used_pct\":75.0") != NULL &&
              strstr(json, "\"root_filesystem\":{\"available\":true,\"total_bytes\":2000,\"available_bytes\":500,\"used_pct\":75.0") != NULL,
          "metrics payload includes CPU, RAM, storage, and warning thresholds");
}

static void test_unavailable_values_and_bounds(void)
{
    HealthReport report = {0};
    report.status = HEALTH_UNKNOWN;
    report.warning_threshold_pct = 70;
    report.cpu_warning_threshold_pct = 60;
    char json[1024];
    int length = web_api_metrics_json(&report, NULL, json, sizeof(json));
    CHECK(length > 0 && strstr(json, "\"sampled_at\":null") != NULL &&
              strstr(json, "\"available\":false,\"used_pct\":null") != NULL &&
              strstr(json, "\"total_kb\":null") != NULL &&
              strstr(json, "\"total_bytes\":null") != NULL,
          "unavailable readings serialize as null instead of zero");
    CHECK(web_api_metrics_json(&report, NULL, json, 8) < 0,
          "metrics serializer rejects a truncated output buffer");
}

static void test_history_records(void)
{
    char path[128];
    (void)snprintf(path, sizeof(path), "/tmp/health-history-%ld.csv", (long)getpid());
    FILE *file = fopen(path, "w");
    CHECK(file != NULL, "history fixture opens");
    if (file == NULL) return;
    fputs("timestamp_utc,memory_used_pct,root_used_pct,warning_threshold_pct,cpu_used_pct,cpu_warning_threshold_pct,status\n", file);
    fputs("2026-11-17T12:00:00Z,30.0,40.0,70,10.0,60,HEALTHY\n", file);
    fputs("malformed,row\n", file);
    fputs("2026-11-17T12:01:00Z,,,70,,60,UNKNOWN\n", file);
    fputs("2026-11-17T12:02:00Z,50.0,20.0,70,80.0,60,WARNING\n", file);
    fclose(file);
    HistorySample samples[WEB_API_HISTORY_MAX_LIMIT]; size_t count = 0;
    int result = web_api_read_history(path, 2, samples, &count);
    CHECK(result == 0 && count == 2 && strcmp(samples[0].sampled_at, "2026-11-17T12:01:00Z") == 0 && strcmp(samples[1].sampled_at, "2026-11-17T12:02:00Z") == 0,
          "history skips malformed rows and returns latest records chronologically");
    CHECK(!samples[0].has_memory_used_pct && !samples[0].has_cpu_used_pct,
          "empty CSV measurement cells remain unavailable");
    char json[1024]; int length = web_api_history_json(samples, count, json, sizeof(json));
    CHECK(length > 0 && strstr(json, "\"api_version\":1,\"samples\":[") != NULL && strstr(json, "\"memory_used_pct\":null") != NULL,
          "history serializer emits versioned JSON and null unavailable values");
    CHECK(web_api_read_history(path, 0, samples, &count) < 0,
          "history reader rejects invalid limit");
    CHECK(web_api_read_history("/tmp/health-history-no-such-file.csv", 10, samples, &count) == 0 && count == 0,
          "missing history file is an empty history");
    CHECK(web_api_read_history(NULL, WEB_API_HISTORY_MAX_LIMIT, samples, &count) == 0 && count == 0,
          "history with no configured CSV path is empty");
    CHECK(web_api_read_history(path, WEB_API_HISTORY_MAX_LIMIT + 1U, samples, &count) < 0,
          "history reader enforces maximum limit");
    size_t limit = 0;
    CHECK(web_api_parse_history_limit(NULL, &limit) == 0 && limit == WEB_API_HISTORY_DEFAULT_LIMIT,
          "history query uses default limit");
    CHECK(web_api_parse_history_limit("limit=1000", &limit) == 0 && limit == WEB_API_HISTORY_MAX_LIMIT,
          "history query accepts maximum limit");
    CHECK(web_api_parse_history_limit("limit=1001", &limit) < 0 && web_api_parse_history_limit("limit=0", &limit) < 0 && web_api_parse_history_limit("limit=x", &limit) < 0 && web_api_parse_history_limit("limit=1&x=2", &limit) < 0,
          "history query rejects invalid and oversized limits");
    remove(path);
}

int main(void)
{
    test_health_payload();
    test_metrics_payload();
    test_unavailable_values_and_bounds();
    test_history_records();
    printf("%u tests, %u failures\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
