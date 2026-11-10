#define _POSIX_C_SOURCE 200809L

#include "gpio_indicator.h"
#include "health_monitor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tiny test harness counters let the executable summarize assertions without
 * requiring an external unit-test framework. */
static unsigned int tests_run;
static unsigned int tests_failed;

/* Count and report each assertion while allowing later checks to run on failure. */
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
    /* Shared healthy fixture: 50% memory/disk usage and safe temperature. */
    HealthReport report = {0};
    report.has_temperature = true;
    report.temperature_c = 25.0;
    report.has_memory = true;
    report.memory_total_kb = 100;
    report.memory_available_kb = 50;
    report.has_disk = true;
    report.disk_total_bytes = 100;
    report.disk_available_bytes = 50;
    report.warning_threshold_pct = 80;
    return report;
}

static void test_healthy_led_off(void)
{
    /* Exercise the complete status-to-mock-output path for a healthy snapshot. */
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
    /* 81% used crosses the configured 80% threshold and should request HIGH. */
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
    /* Storage uses the same utilization rule as memory. */
    HealthReport report = base_report();
    report.disk_available_bytes = 19;
    report.status = evaluate_health_status(&report);
    CHECK(report.status == HEALTH_WARNING,
          "storage utilization above threshold produces WARNING");
}

static void test_exact_threshold(void)
{
    /* Boundary assertion: exactly 80% used is not an exceedance. */
    HealthReport report = base_report();
    report.memory_available_kb = 20;
    report.disk_available_bytes = 20;
    CHECK(evaluate_health_status(&report) == HEALTHY,
          "exactly reaching the memory and storage threshold remains HEALTHY");
}

static void test_exceeding_threshold(void)
{
    /* One percentage point over the threshold must change overall status. */
    HealthReport report = base_report();
    report.disk_available_bytes = 19;
    CHECK(evaluate_health_status(&report) == HEALTH_WARNING,
          "exceeding the configured utilization threshold produces WARNING");
}

static void test_critical_led_on(void)
{
    /* Critical CPU status shares the warning LED's HIGH behavior. */
    GpioIndicator led = {0};
    char error[256] = {0};
    (void)gpio_indicator_init_mock(&led, error, sizeof(error));
    int result = gpio_indicator_apply_status(&led, HEALTH_CRITICAL,
                                             error, sizeof(error));
    CHECK(result == 0 && led.mock_output,
          "CRITICAL status also drives the mock LED HIGH/ON");
    (void)gpio_indicator_close(&led, error, sizeof(error));
}

static void test_temperature_critical_remains_critical(void)
{
    /* A resource warning must not downgrade a more serious temperature result. */
    HealthReport report = base_report();
    report.temperature_c = 80.0;
    report.memory_available_kb = 19;
    report.status = evaluate_health_status(&report);
    CHECK(report.status == HEALTH_CRITICAL,
          "resource threshold warning does not downgrade CPU-temperature CRITICAL");
}

static void test_gpio_initialization_failure(void)
{
    /* Point hardware initialization at a deliberately nonexistent chip to check
     * error reporting without requiring a physical device. */
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
    /* Verify cleanup requests LOW after HIGH and invalidates mock lifecycle state. */
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
    /* Each case is independent; aggregate failures determine process exit status. */
    test_healthy_led_off();
    test_memory_warning();
    test_storage_warning();
    test_exact_threshold();
    test_exceeding_threshold();
    test_critical_led_on();
    test_temperature_critical_remains_critical();
    test_gpio_initialization_failure();
    test_gpio_cleanup();
    printf("%u tests run; %u failed.\n", tests_run, tests_failed);
    return tests_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
