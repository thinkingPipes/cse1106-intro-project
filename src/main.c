#include "health_monitor.h"
#include "gpio_indicator.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep option help in one place so invalid options can show the same usage. */
static void print_usage(const char *program)
{
    printf("Usage: %s [--log FILE] [THRESHOLD_PERCENT] [--help]\n", program);
    puts("Report Linux equipment health metrics.");
    puts("  --log FILE  Append this report to a CSV file");
    puts("  THRESHOLD   Memory/storage warning threshold (integer 1-100; default 80)");
    puts("  --help      Show this help message");
    puts("  --no-gpio   Run monitoring without requesting a physical LED");
    puts("  --mock-gpio Simulate LED output by printing HIGH/LOW requests");
}

/* Accept decimal digits only, then range-check before narrowing to unsigned int. */
static int parse_threshold(const char *text, unsigned int *threshold)
{
    if (*text == '\0') {
        return -1;
    }
    for (const char *digit = text; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9') {
            return -1;
        }
    }

    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < 1 ||
        value > 100 || value > UINT_MAX) {
        return -1;
    }
    *threshold = (unsigned int)value;
    return 0;
}

int main(int argc, char **argv)
{
    /* CLI settings are collected first; the report then flows to console, GPIO,
     * and optional CSV output. */
    const char *log_path = NULL;
    unsigned int warning_threshold_pct = 80;
    bool threshold_supplied = false;
    bool gpio_disabled = false;
    bool gpio_mocked = false;

    /* Options may appear in any order; a positional threshold is allowed once. */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--no-gpio") == 0) {
            gpio_disabled = true;
            continue;
        }
        if (strcmp(argv[i], "--mock-gpio") == 0) {
            gpio_mocked = true;
            continue;
        }
        if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
            continue;
        }

        if (argv[i][0] != '-') {
            if (threshold_supplied ||
                parse_threshold(argv[i], &warning_threshold_pct) != 0) {
                fprintf(stderr,
                        "Invalid threshold '%s': enter an integer from 1 to 100.\n",
                        argv[i]);
                return 2;
            }
            threshold_supplied = true;
            continue;
        }

        if (argv[i][0] == '+' ||
            (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9')) {
            fprintf(stderr,
                    "Invalid threshold '%s': enter an integer from 1 to 100.\n",
                    argv[i]);
            return 2;
        }

        fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
        print_usage(argv[0]);
        return 2;
    }

    if (gpio_disabled && gpio_mocked) {
        fputs("Choose either --no-gpio or --mock-gpio, not both.\n", stderr);
        return 2;
    }

    /* Initialize the chosen backend before reporting, so setup failures are
     * surfaced while still allowing available health metrics to be printed. */
    GpioIndicator indicator = {0};
    bool gpio_ready = false;
    bool gpio_failure = false;
    char gpio_error[256];
    if (gpio_disabled) {
        puts("Warning LED:          disabled (--no-gpio)");
    } else if (gpio_mocked) {
        if (gpio_indicator_init_mock(&indicator, gpio_error,
                                     sizeof(gpio_error)) == 0) {
            gpio_ready = true;
            puts("Warning LED:          mock backend (no physical GPIO)");
        } else {
            fprintf(stderr, "Warning LED mock initialization failed: %s\n",
                    gpio_error);
            gpio_failure = true;
        }
    } else if (gpio_indicator_init(&indicator, gpio_error,
                                   sizeof(gpio_error)) == 0) {
        gpio_ready = true;
    } else {
        fprintf(stderr, "Warning LED unavailable: %s\n", gpio_error);
        gpio_failure = true;
    }

    /* Collector owns metric acquisition and status evaluation; main coordinates
     * the resulting snapshot with independent output modules. */
    HealthReport report;
    collect_health_report(&report, warning_threshold_pct);
    print_health_report(&report);

    /* Status maps to a single output request: warning/critical HIGH, otherwise LOW. */
    if (gpio_ready) {
        if (gpio_indicator_apply_status(&indicator, report.status, gpio_error,
                                        sizeof(gpio_error)) < 0) {
            fprintf(stderr, "Warning LED update failed: %s\n", gpio_error);
            gpio_failure = true;
        } else {
            printf("Warning LED:          %s\n",
                   (report.status == HEALTH_WARNING ||
                    report.status == HEALTH_CRITICAL) ? "ON" : "OFF");
        }
    }

    if (log_path != NULL && append_health_report(log_path, &report) != 0) {
        fprintf(stderr, "Could not append report to %s\n", log_path);
        gpio_failure = true;
    }

    /* Close always gets a chance to request LOW and release hardware resources. */
    if (gpio_indicator_close(&indicator, gpio_error, sizeof(gpio_error)) < 0) {
        fprintf(stderr, "Could not ensure warning LED is off during cleanup: %s\n",
                gpio_error);
        gpio_failure = true;
    }
    return gpio_failure ? 1 : 0;
}
