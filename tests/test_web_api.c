#include "web_api.h"

#include <stdio.h>
#include <string.h>

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

int main(void)
{
    test_health_payload();
    test_metrics_payload();
    test_unavailable_values_and_bounds();
    printf("%u tests, %u failures\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
