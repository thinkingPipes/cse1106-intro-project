#define _POSIX_C_SOURCE 200809L

#include "gpio_indicator.h"
#include "health_monitor.h"

#include <errno.h>
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

static HealthReport base_report(void)
{
    
    HealthReport report = {0};
    report.has_memory = true;
    report.memory_total_kb = 100;
    report.memory_available_kb = 50;
    report.has_disk = true;
    report.disk_total_bytes = 100;
    report.disk_available_bytes = 50;
    report.warning_threshold_pct = 70;
    report.cpu_warning_threshold_pct = 60;
    return report;
}

static void test_healthy_led_off(void)
{
    
    HealthReport report = base_report();
    GpioIndicator led = {0};
    char error[256] = {0};
    (void)gpio_indicator_init_mock(&led, error, sizeof(error));
    report.status = evaluate_health_status(&report);
    int apply_result = gpio_indicator_apply_status(&led, report.status,
                                                   error, sizeof(error));
    bool off_before_close = !led.mock_output;
    (void)gpio_indicator_close(&led, error, sizeof(error));
    CHECK(report.status == HEALTHY && apply_result == 0 && off_before_close,
          "HEALTHY status drives mock LED LOW/OFF");
}

static void test_memory_warning(void)
{
    
    HealthReport report = base_report();
    report.memory_available_kb = 19;
    report.status = evaluate_health_status(&report);
    GpioIndicator led = {0};
    char error[256] = {0};
    (void)gpio_indicator_init_mock(&led, error, sizeof(error));
    int result = gpio_indicator_apply_status(&led, report.status,
                                             error, sizeof(error));
    CHECK(report.status == HEALTH_WARNING && result == 0 && led.mock_output,
          "memory utilization above threshold produces WARNING and LED HIGH");
    (void)gpio_indicator_close(&led, error, sizeof(error));
}

static void test_storage_warning(void)
{
    
    HealthReport report = base_report();
    report.disk_available_bytes = 19;
    report.status = evaluate_health_status(&report);
    CHECK(report.status == HEALTH_WARNING,
          "storage utilization above threshold produces WARNING");
}

static void test_exact_threshold(void)
{
    
    HealthReport report = base_report();
    report.memory_available_kb = 30;
    report.disk_available_bytes = 30;
    CHECK(evaluate_health_status(&report) == HEALTHY,
          "exactly reaching the memory and storage threshold remains HEALTHY");
}

static void test_exceeding_threshold(void)
{
    
    HealthReport report = base_report();
    report.disk_available_bytes = 19;
    CHECK(evaluate_health_status(&report) == HEALTH_WARNING,
          "exceeding the configured utilization threshold produces WARNING");
}

static void test_cpu_stat_parser(void)
{
    uint64_t total_ticks = 0;
    uint64_t idle_ticks = 0;
    bool parsed = parse_cpu_stat_line(
        "cpu 10 2 3 40 5 6 7 8 9 10\n", &total_ticks, &idle_ticks);
    CHECK(parsed && total_ticks == 81 && idle_ticks == 45,
          "aggregate CPU parser sums counters and includes idle plus iowait");
    CHECK(!parse_cpu_stat_line("cpu 10 2 3\n", &total_ticks, &idle_ticks) &&
              !parse_cpu_stat_line("cpu nope 2 3 4\n", &total_ticks,
                                   &idle_ticks) &&
              !parse_cpu_stat_line("cpu0 10 2 3 4\n", &total_ticks,
                                   &idle_ticks),
          "CPU parser rejects missing, malformed, and per-core rows");
}

static void test_cpu_sample_deltas(void)
{
    CpuSampleState state = {0};
    double utilization = -1.0;
    CHECK(!update_cpu_utilization(&state, 100, 50, &utilization) &&
              state.has_previous,
          "first CPU sample records a baseline without reporting utilization");
    CHECK(update_cpu_utilization(&state, 200, 100, &utilization) &&
              utilization == 50.0,
          "successive CPU counters calculate busy time as a percentage");
    CHECK(!update_cpu_utilization(&state, 10, 5, &utilization) &&
              state.previous_total_ticks == 10,
          "counter reset safely establishes a new baseline");
    CHECK(update_cpu_utilization(&state, 110, 55, &utilization) &&
              utilization == 50.0,
          "CPU sampling resumes after a counter reset");
    CHECK(!update_cpu_utilization(&state, 210, 250, &utilization),
          "invalid idle counters do not produce a CPU percentage");
}

static void test_cpu_thresholds(void)
{
    HealthReport report = base_report();
    report.has_cpu = true;
    report.cpu_used_pct = 60.0;
    CHECK(evaluate_health_status(&report) == HEALTHY,
          "CPU usage exactly at its separate threshold remains HEALTHY");
    report.cpu_used_pct = 60.1;
    CHECK(evaluate_health_status(&report) == HEALTH_WARNING,
          "CPU usage above its separate threshold produces WARNING");
    report.has_memory = false;
    report.has_disk = false;
    CHECK(evaluate_health_status(&report) == HEALTH_WARNING,
          "CPU utilization alone contributes to overall status");
    report.has_cpu = false;
    CHECK(evaluate_health_status(&report) == HEALTH_UNKNOWN,
          "no valid CPU, memory, or storage reading yields UNKNOWN");
}

static void test_cpu_csv_logging(void)
{
    char path[128];
    (void)snprintf(path, sizeof(path), "/tmp/health-monitor-test-%ld.csv",
                   (long)getpid());
    (void)remove(path);
    HealthReport report = base_report();
    report.has_cpu = true;
    report.cpu_used_pct = 42.5;
    report.status = HEALTHY;
    int first_result = append_health_report(path, &report);
    int second_result = append_health_report(path, &report);
    FILE *file = fopen(path, "r");
    char header[256] = {0};
    char first_row[256] = {0};
    char second_row[256] = {0};
    bool rows_read = file != NULL && fgets(header, sizeof(header), file) != NULL &&
                     fgets(first_row, sizeof(first_row), file) != NULL &&
                     fgets(second_row, sizeof(second_row), file) != NULL;
    if (file != NULL) {
        fclose(file);
    }
    (void)remove(path);
    CHECK(first_result == 0 && second_result == 0 && rows_read &&
              strstr(header, "cpu_used_pct") != NULL &&
              strstr(header, "cpu_warning_threshold_pct") != NULL &&
              strstr(first_row, ",42.5,60,HEALTHY\n") != NULL,
          "CSV log includes CPU utilization and its threshold on every row");
}

static void test_legacy_csv_is_preserved(void)
{
    char path[128];
    (void)snprintf(path, sizeof(path), "/tmp/health-monitor-legacy-%ld.csv",
                   (long)getpid());
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        CHECK(false, "legacy CSV fixture can be created");
        return;
    }
    fputs("timestamp_utc,memory_used_pct,root_used_pct,warning_threshold_pct,status\n"
          "2026-01-01T00:00:00Z,50.0,50.0,70,HEALTHY\n", file);
    fclose(file);
    HealthReport report = base_report();
    errno = 0;
    int result = append_health_report(path, &report);
    int append_error = errno;
    file = fopen(path, "r");
    char header[256] = {0};
    char original_row[256] = {0};
    char extra_row[256] = {0};
    bool unchanged = file != NULL && fgets(header, sizeof(header), file) != NULL &&
                     fgets(original_row, sizeof(original_row), file) != NULL &&
                     fgets(extra_row, sizeof(extra_row), file) == NULL;
    if (file != NULL) {
        fclose(file);
    }
    (void)remove(path);
    CHECK(result < 0 && append_error == EINVAL && unchanged,
          "legacy CSV is rejected without changing its existing rows");
}

static void test_gpio_initialization_failure(void)
{
    
    GpioIndicator led = {0};
    char error[256] = {0};
    (void)setenv("HEALTH_MONITOR_GPIO_CHIP",
                 "/dev/health-monitor-test-no-such-chip", 1);
    int result = gpio_indicator_init(&led, error, sizeof(error));
    (void)unsetenv("HEALTH_MONITOR_GPIO_CHIP");
    CHECK(result != 0 && error[0] != '\0',
          "GPIO initialization failure returns an informative error");
    (void)gpio_indicator_close(&led, error, sizeof(error));
}

static void test_gpio_cleanup(void)
{
    
    GpioIndicator led = {0};
    char error[256] = {0};
    int init_result = gpio_indicator_init_mock(&led, error, sizeof(error));
    int high_result = gpio_indicator_set(&led, true, error, sizeof(error));
    int close_result = gpio_indicator_close(&led, error, sizeof(error));
    CHECK(init_result == 0 && high_result == 0 && close_result == 0 &&
              !led.initialized && !led.mock_output &&
              led.mock_high_requests == 1 && led.mock_low_requests >= 2,
          "GPIO cleanup requests LOW and leaves the mock LED off");
}

int main(void)
{
    
    test_healthy_led_off();
    test_memory_warning();
    test_storage_warning();
    test_exact_threshold();
    test_exceeding_threshold();
    test_cpu_stat_parser();
    test_cpu_sample_deltas();
    test_cpu_thresholds();
    test_cpu_csv_logging();
    test_legacy_csv_is_preserved();
    test_gpio_initialization_failure();
    test_gpio_cleanup();
    printf("%u tests run; %u failed.\n", tests_run, tests_failed);
    return tests_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
