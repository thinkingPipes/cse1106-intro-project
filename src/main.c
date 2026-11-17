#define _POSIX_C_SOURCE 200809L

#include "health_monitor.h"
#include "gpio_indicator.h"
#include "web_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t stop_requested;

static void handle_stop_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void print_usage(const char *program)
{
    printf("Usage: %s [--host ADDRESS] [--port PORT] [--web-root DIR] [--log FILE] [--interval SECONDS] [THRESHOLD_PERCENT] [--help]\n",
           program);
    puts("Monitor Linux equipment and serve its health data over HTTP.");
    puts("  --host ADDRESS  Bind address (default 127.0.0.1)");
    puts("  --port PORT     HTTP port (default 8080; range 1-65535)");
    puts("  --web-root DIR  Static web asset directory (default web)");
    puts("  --log FILE  Append this report to a CSV file");
    puts("  --interval  Seconds between reports (1-3600; default 5)");
    puts("  --cpu-threshold PCT  CPU warning threshold (integer 1-100; default 60)");
    puts("  THRESHOLD   RAM/storage warning threshold (integer 1-100; default 70)");
    puts("  --help      Show this help message");
}

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

static int parse_interval(const char *text, unsigned int *interval)
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
        value > 3600 || value > UINT_MAX) {
        return -1;
    }
    *interval = (unsigned int)value;
    return 0;
}

static int parse_port(const char *text, unsigned int *port)
{
    if (text == NULL || text[0] == '\0') {
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
        value > 65535 || value > UINT_MAX) {
        return -1;
    }
    *port = (unsigned int)value;
    return 0;
}

static int wait_interval(unsigned int seconds)
{
    struct timespec remaining = {.tv_sec = (time_t)seconds, .tv_nsec = 0};
    while (!stop_requested && nanosleep(&remaining, &remaining) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *log_path = NULL;
    const char *host = "127.0.0.1";
    const char *web_root = "web";
    unsigned int port = 8080;
    unsigned int warning_threshold_pct = 70;
    unsigned int cpu_warning_threshold_pct = 60;
    unsigned int interval_seconds = 5;
    bool threshold_supplied = false;
    bool cpu_threshold_supplied = false;
    bool interval_supplied = false;
    bool host_supplied = false;
    bool port_supplied = false;
    bool web_root_supplied = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            struct in_addr address;
            if (host_supplied || inet_pton(AF_INET, value, &address) != 1) {
                fprintf(stderr, "Invalid host address. Use an IPv4 address such as 127.0.0.1 or 0.0.0.0.\n");
                return 2;
            }
            host = value;
            host_supplied = true;
            continue;
        }
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            if (port_supplied || parse_port(argv[++i], &port) != 0) {
                fprintf(stderr, "Invalid port: enter an integer from 1 to 65535.\n");
                return 2;
            }
            port_supplied = true;
            continue;
        }
        if (strcmp(argv[i], "--web-root") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (web_root_supplied || value[0] == '\0') {
                fprintf(stderr, "Invalid web root directory.\n");
                return 2;
            }
            web_root = value;
            web_root_supplied = true;
            continue;
        }
        if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            if (interval_supplied ||
                parse_interval(argv[++i], &interval_seconds) != 0) {
                fprintf(stderr,
                        "Invalid interval: enter an integer from 1 to 3600 seconds.\n");
                return 2;
            }
            interval_supplied = true;
            continue;
        }
        if (strcmp(argv[i], "--cpu-threshold") == 0 && i + 1 < argc) {
            if (cpu_threshold_supplied ||
                parse_threshold(argv[++i], &cpu_warning_threshold_pct) != 0) {
                fprintf(stderr,
                        "Invalid CPU threshold: enter an integer from 1 to 100.\n");
                return 2;
            }
            cpu_threshold_supplied = true;
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

    struct sigaction action = {0};
    action.sa_handler = handle_stop_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0) {
        perror("Could not install shutdown signal handlers");
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);

    GpioIndicator indicator = {0};
    bool operation_failed = false;
    char gpio_error[256];
#ifdef HEALTH_MONITOR_TEST
    if (gpio_indicator_init_mock(&indicator, gpio_error,
                                 sizeof(gpio_error)) < 0) {
#else
    if (gpio_indicator_init(&indicator, gpio_error, sizeof(gpio_error)) < 0) {
#endif
        fprintf(stderr, "Warning LED unavailable: %s\n", gpio_error);
        return 1;
    }

    WebServer server = {0};
    char web_error[256];
    if (web_server_start(&server, host, port, web_root, log_path, web_error,
                         sizeof(web_error)) < 0) {
        fprintf(stderr, "Could not start HTTP server: %s\n", web_error);
        if (gpio_indicator_close(&indicator, gpio_error,
                                 sizeof(gpio_error)) < 0) {
            fprintf(stderr, "Could not turn warning LED off during cleanup: %s\n",
                    gpio_error);
        }
        return 1;
    }
    printf("Monitoring every %u seconds; HTTP server bound to %s:%u (Ctrl+C to stop).\n",
           interval_seconds, host, port);
    CpuSampleState cpu_state = {0};
    while (!stop_requested) {
        HealthReport report;
        collect_health_report(&report, warning_threshold_pct,
                              cpu_warning_threshold_pct, &cpu_state);
        print_health_report(&report);
        if (gpio_indicator_apply_status(&indicator, report.status, gpio_error,
                                        sizeof(gpio_error)) < 0) {
            fprintf(stderr, "Warning LED update failed: %s\n", gpio_error);
            operation_failed = true;
        } else {
            printf("Warning LED:          %s\n",
                   report.status == HEALTH_WARNING ? "ON" : "OFF");
        }
        if (log_path != NULL && append_health_report(log_path, &report) != 0) {
            if (errno == EINVAL) {
                fprintf(stderr,
                        "CSV file %s has an older or invalid header; choose a new log file for CPU columns.\n",
                        log_path);
                log_path = NULL;
            } else {
                fprintf(stderr, "Could not append report to %s\n", log_path);
            }
            operation_failed = true;
        }
        char timestamp_utc[32];
        time_t now = time(NULL);
        struct tm utc_time;
        bool has_timestamp = now != (time_t)-1 &&
                             gmtime_r(&now, &utc_time) != NULL &&
                             strftime(timestamp_utc, sizeof(timestamp_utc),
                                      "%Y-%m-%dT%H:%M:%SZ", &utc_time) > 0;
        if (has_timestamp) {
            web_server_update(&server, &report, timestamp_utc);
        } else {
            web_server_update(&server, &report, NULL);
        }
        if (stop_requested || wait_interval(interval_seconds) < 0) {
            if (!stop_requested) {
                perror("Monitoring interval wait failed");
                operation_failed = true;
            }
            break;
        }
    }

    web_server_stop(&server);
    if (gpio_indicator_close(&indicator, gpio_error, sizeof(gpio_error)) < 0) {
        fprintf(stderr, "Could not ensure warning LED is off during cleanup: %s\n",
                gpio_error);
        operation_failed = true;
    }
    puts("Monitoring stopped; warning LED is off.");
    return operation_failed ? 1 : 0;
}
