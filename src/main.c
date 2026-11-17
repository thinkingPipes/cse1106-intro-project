#define _POSIX_C_SOURCE 200809L

#include "health_monitor.h"
#include "gpio_indicator.h"
#include "web_server.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <spawn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;

extern char **environ;

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

static void print_control_help(void)
{
    puts("\nCommands (press Enter after a command):");
    puts("  h  Show this help");
    puts("  q  Stop the monitor and web server");
}

static void print_startup_banner(const char *host, unsigned int port)
{
    const char *dashboard_host = host;
    if (strcmp(host, "0.0.0.0") == 0) {
        dashboard_host = "127.0.0.1";
    }

    printf("\nRaspberry Pi Equipment Health Monitor\n");
    puts("Web server started.");
    printf("Dashboard: http://%s:%u/\n", dashboard_host, port);
    if (strcmp(host, "0.0.0.0") == 0) {
        printf("From another device, open http://<PI-LAN-IP>:%u/\n", port);
    } else if (strcmp(host, "127.0.0.1") == 0) {
        printf("For LAN access, restart with --host 0.0.0.0, then open\n");
        printf("http://<PI-LAN-IP>:%u/ from the other device.\n", port);
    } else {
        printf("From another device on this network, open http://%s:%u/\n",
               host, port);
    }
    print_control_help();
}

static bool graphical_session_available(void)
{
    const char *display = getenv("DISPLAY");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    return (display != NULL && display[0] != '\0') ||
           (wayland_display != NULL && wayland_display[0] != '\0');
}

static pid_t launch_browser(const char *url)
{
    if (!graphical_session_available()) {
        puts("No graphical session detected; open the dashboard URL manually.");
        return -1;
    }

    /* Pass the URL as an argument instead of invoking a shell. */
    char *arguments[] = {(char *)"xdg-open", (char *)url, NULL};
    pid_t child = -1;
    int result = posix_spawnp(&child, "xdg-open", NULL, NULL, arguments,
                              environ);
    if (result != 0) {
        fprintf(stderr,
                "Could not launch xdg-open (%s); open the dashboard URL manually.\n",
                strerror(result));
        return -1;
    }
    puts("Asked xdg-open to open the dashboard.");
    return child;
}

/* Reap an exited xdg-open child without waiting for a browser to close. */
static void reap_browser(pid_t *browser_child)
{
    if (browser_child == NULL || *browser_child <= 0) {
        return;
    }
    int status;
    pid_t result;
    do {
        result = waitpid(*browser_child, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == *browser_child) {
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            puts("Automatic browser opening failed; open the dashboard URL manually.");
        }
        *browser_child = -1;
    } else if (result < 0 && errno == ECHILD) {
        *browser_child = -1;
    }
}

static bool handle_control_line(char *line)
{
    char *command = line;
    while (isspace((unsigned char)*command)) {
        ++command;
    }
    char *end = command + strlen(command);
    while (end > command && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    if (strcmp(command, "h") == 0) {
        print_control_help();
    } else if (strcmp(command, "q") == 0) {
        stop_requested = 1;
    } else if (command[0] != '\0') {
        printf("Unknown command: %s\n", command);
        print_control_help();
    }
    return !stop_requested;
}

/* Poll stdin until the next sample deadline; completed terminal lines are
 * available after Enter when the terminal is in its normal canonical mode. */
static int wait_for_input_or_timeout(unsigned int seconds, bool *input_open,
                                     pid_t *browser_child)
{
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0) {
        return -1;
    }
    deadline.tv_sec += (time_t)seconds;

    char line[128];
    size_t line_length = 0;
    bool discard_line = false;
    while (!stop_requested) {
        reap_browser(browser_child);
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
            return -1;
        }
        time_t remaining_seconds = deadline.tv_sec - now.tv_sec;
        long remaining_nanoseconds = deadline.tv_nsec - now.tv_nsec;
        if (remaining_nanoseconds < 0) {
            --remaining_seconds;
            remaining_nanoseconds += 1000000000L;
        }
        if (remaining_seconds < 0 ||
            (remaining_seconds == 0 && remaining_nanoseconds <= 0)) {
            return 0;
        }
        long long timeout_ms = (long long)remaining_seconds * 1000LL +
                               (remaining_nanoseconds + 999999L) / 1000000L;
        if (timeout_ms > INT_MAX) {
            timeout_ms = INT_MAX;
        }

        struct pollfd input = {
            .fd = input_open != NULL && *input_open ? STDIN_FILENO : -1,
            .events = POLLIN,
            .revents = 0
        };
        int poll_result = poll(&input, 1, (int)timeout_ms);
        if (poll_result == 0) {
            return 0;
        }
        if (poll_result < 0) {
            if (errno == EINTR && !stop_requested) {
                continue;
            }
            return stop_requested ? 0 : -1;
        }
        if ((input.revents & (POLLIN | POLLHUP)) != 0 &&
            input.fd >= 0) {
            char bytes[128];
            ssize_t count = read(STDIN_FILENO, bytes, sizeof(bytes));
            if (count == 0) {
                if (input_open != NULL) {
                    *input_open = false;
                }
            } else if (count < 0) {
                if (errno != EINTR && errno != EAGAIN) {
                    if (input_open != NULL) {
                        *input_open = false;
                    }
                }
            } else {
                for (ssize_t i = 0; i < count; ++i) {
                    char byte = bytes[i];
                    if (byte == '\n' || byte == '\r') {
                        if (!discard_line) {
                            line[line_length] = '\0';
                            if (!handle_control_line(line)) {
                                return 0;
                            }
                        } else {
                            puts("Command too long; enter h or q.");
                        }
                        line_length = 0;
                        discard_line = false;
                    } else if (!discard_line) {
                        if (line_length + 1 < sizeof(line)) {
                            line[line_length++] = byte;
                        } else {
                            discard_line = true;
                        }
                    }
                }
            }
        }
        if ((input.revents & (POLLERR | POLLNVAL)) != 0 &&
            input_open != NULL) {
            *input_open = false;
        }
    }
    return 0;
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
    char dashboard_url[INET_ADDRSTRLEN + 32];
    const char *dashboard_host = strcmp(host, "0.0.0.0") == 0
                                     ? "127.0.0.1"
                                     : host;
    (void)snprintf(dashboard_url, sizeof(dashboard_url), "http://%s:%u/",
                   dashboard_host, port);
    print_startup_banner(host, port);
    pid_t browser_child = launch_browser(dashboard_url);
    bool input_open = true;

    CpuSampleState cpu_state = {0};
    while (!stop_requested) {
        HealthReport report;
        collect_health_report(&report, warning_threshold_pct,
                              cpu_warning_threshold_pct, &cpu_state);
        if (gpio_indicator_apply_status(&indicator, report.status, gpio_error,
                                        sizeof(gpio_error)) < 0) {
            fprintf(stderr, "Warning LED update failed: %s\n", gpio_error);
            operation_failed = true;
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
        if (stop_requested ||
            wait_for_input_or_timeout(interval_seconds, &input_open,
                                      &browser_child) < 0) {
            if (!stop_requested) {
                perror("Monitoring interval wait failed");
                operation_failed = true;
            }
            break;
        }
    }

    reap_browser(&browser_child);
    web_server_stop(&server);
    if (gpio_indicator_close(&indicator, gpio_error, sizeof(gpio_error)) < 0) {
        fprintf(stderr, "Could not ensure warning LED is off during cleanup: %s\n",
                gpio_error);
        operation_failed = true;
    }
    puts("Monitoring stopped; warning LED is off.");
    return operation_failed ? 1 : 0;
}
