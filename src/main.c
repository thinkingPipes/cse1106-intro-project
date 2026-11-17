#define _POSIX_C_SOURCE 200809L

#include "health_monitor.h"
#include "gpio_indicator.h"
#include "web_server.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <spawn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;

extern char **environ;

typedef struct {
    pid_t pid;
    unsigned int test_id;
    const char *name;
} RunningTest;

static void handle_stop_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static unsigned int cli_width(void)
{
    struct winsize terminal_size = {0};
    if (isatty(STDOUT_FILENO) &&
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &terminal_size) == 0 &&
        terminal_size.ws_col > 0) {
        return terminal_size.ws_col < 72 ? terminal_size.ws_col : 72;
    }
    return 72;
}

static void print_separator(char character)
{
    unsigned int width = cli_width();
    for (unsigned int column = 0; column < width; ++column) {
        putchar(character);
    }
    putchar('\n');
}

static void print_wrapped(const char *text, unsigned int indent)
{
    unsigned int width = cli_width();
    if (indent >= width) {
        indent = 0;
    }
    unsigned int column = indent;
    for (unsigned int space = 0; space < indent; ++space) {
        putchar(' ');
    }

    const char *word = text;
    while (*word != '\0') {
        while (isspace((unsigned char)*word)) {
            ++word;
        }
        if (*word == '\0') {
            break;
        }
        const char *end = word;
        while (*end != '\0' && !isspace((unsigned char)*end)) {
            ++end;
        }
        unsigned int length = (unsigned int)(end - word);
        if (column > indent && column + 1 + length > width) {
            putchar('\n');
            for (unsigned int space = 0; space < indent; ++space) {
                putchar(' ');
            }
            column = indent;
        } else if (column > indent) {
            putchar(' ');
            ++column;
        }

        for (const char *character = word; character < end; ++character) {
            if (column == width) {
                putchar('\n');
                for (unsigned int space = 0; space < indent; ++space) {
                    putchar(' ');
                }
                column = indent;
            }
            putchar(*character);
            ++column;
        }
        word = end;
    }
    putchar('\n');
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
    puts("After startup, enter h for help, t for test menu, or q to quit.");
}

static void print_control_help(void)
{
    putchar('\n');
    print_separator('-');
    puts("CONTROLS  (press Enter after a command)");
    puts("  h  Show commands and test-menu help");
    puts("  t  Open the hardware test menu");
    puts("  q  Gracefully stop the monitor and web server");
    puts("  Full procedures: docs/test-cases.md");
    print_separator('-');
}

static void print_test_menu(void)
{
    putchar('\n');
    print_separator('-');
    puts("HARDWARE TESTS  (enter a number, then press Enter)");
    puts("  1  CPU escalation (4 cores)");
    puts("  2  RAM escalation (1 GB reserve)");
    puts("  3  Healthy baseline");
    puts("  4  Recovery after stress");
    puts("  5  Filesystem check");
    puts("  Full procedures: docs/test-cases.md");
    print_separator('-');
}

static void print_startup_banner(const char *host, unsigned int port)
{
    const char *dashboard_host = host;
    if (strcmp(host, "0.0.0.0") == 0) {
        dashboard_host = "127.0.0.1";
    }

    putchar('\n');
    print_separator('=');
    puts("Raspberry Pi Equipment Health Monitor");
    print_separator('=');
    puts("\nSERVER");
    puts("  Web server started and monitoring is active.");
    puts("\nDASHBOARD");
    printf("  http://%s:%u/\n", dashboard_host, port);
    if (strcmp(host, "0.0.0.0") == 0) {
        puts("  From another device:");
        printf("  http://<PI-LAN-IP>:%u/\n", port);
    } else if (strcmp(host, "127.0.0.1") == 0) {
        puts("  LAN access: restart with --host 0.0.0.0, then use:");
        printf("  http://<PI-LAN-IP>:%u/\n", port);
    } else {
        puts("  From another device on this network:");
        printf("  http://%s:%u/\n", host, port);
    }
    puts("\nCONTROLS");
    puts("  h  Help     t  Test menu     q  Quit");
    print_separator('-');
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

static void report_test_result(const RunningTest *test, int status)
{
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) {
            printf("Test %u (%s) finished successfully.\n", test->test_id,
                   test->name);
        } else {
            printf("Test %u (%s) finished with exit status %d.\n",
                   test->test_id, test->name, exit_code);
        }
    } else if (WIFSIGNALED(status)) {
        printf("Test %u (%s) stopped by signal %d.\n", test->test_id,
               test->name, WTERMSIG(status));
    } else {
        printf("Test %u (%s) ended with an unrecognized process status.\n",
               test->test_id, test->name);
    }
}

static void reap_test(RunningTest *test)
{
    if (test == NULL || test->pid <= 0) {
        return;
    }
    int status;
    pid_t result;
    do {
        result = waitpid(test->pid, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == test->pid) {
        report_test_result(test, status);
        test->pid = -1;
        test->test_id = 0;
        test->name = NULL;
    } else if (result < 0 && errno == ECHILD) {
        printf("Test %u process is no longer available to wait for.\n",
               test->test_id);
        test->pid = -1;
        test->test_id = 0;
        test->name = NULL;
    }
}

static const char *test_name(unsigned int test_id)
{
    switch (test_id) {
    case 1:
        return "CPU load escalation";
    case 2:
        return "RAM allocation escalation";
    default:
        return "unknown test";
    }
}

static void start_stress_test(unsigned int test_id, RunningTest *running_test)
{
    if (running_test->pid > 0) {
        printf("Test %u (%s) is already running; wait for it to finish.\n",
               running_test->test_id, running_test->name);
        return;
    }

    const char *script = test_id == 1 ? "tests/cpu_escalation.sh"
                                      : "tests/ram_escalation.sh";
    char *arguments[] = {(char *)script, NULL};
    printf("\nLaunching Test %u: %s\n", test_id, test_name(test_id));
    (void)fflush(stdout);
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_t file_actions;
    int result = posix_spawnattr_init(&attributes);
    if (result != 0) {
        fprintf(stderr, "Could not prepare test process: %s\n",
                strerror(result));
        return;
    }
    /* Give the script and its stress-ng child a group that shutdown can signal. */
    result = posix_spawnattr_setpgroup(&attributes, 0);
    if (result == 0) {
        result = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    }
    bool actions_initialized = false;
    if (result == 0) {
        result = posix_spawn_file_actions_init(&file_actions);
        actions_initialized = result == 0;
    }
    /* Keep keyboard commands with the monitor, not the test script. */
    if (result == 0) {
        result = posix_spawn_file_actions_addopen(&file_actions, STDIN_FILENO,
                                                  "/dev/null", O_RDONLY, 0);
    }

    pid_t child = -1;
    if (result == 0) {
        result = posix_spawn(&child, script, &file_actions, &attributes,
                             arguments, environ);
    }
    if (actions_initialized) {
        (void)posix_spawn_file_actions_destroy(&file_actions);
    }
    (void)posix_spawnattr_destroy(&attributes);
    if (result != 0) {
        if (result == ENOENT || result == EACCES) {
            fprintf(stderr, "Could not start %s at %s.\n",
                    test_name(test_id), script);
            fputs("  Run the monitor from the project directory and check\n"
                  "  that the test script is executable.\n", stderr);
        } else {
            fprintf(stderr, "Could not start %s: %s\n", test_name(test_id),
                    strerror(result));
        }
        return;
    }

    running_test->pid = child;
    running_test->test_id = test_id;
    running_test->name = test_name(test_id);
    printf("  Status  : Running (process %ld)\n", (long)child);
    puts("  Monitor sampling and LED updates continue.");
    puts("  Test output follows.\n");
}

static void stop_running_test(RunningTest *test)
{
    if (test == NULL || test->pid <= 0) {
        return;
    }

    pid_t child = test->pid;
    printf("\nShutdown requested while Test %u (%s) is running.\n",
           test->test_id, test->name);
    puts("  Sending SIGTERM to the test process group.");
    if (kill(-child, SIGTERM) < 0 && errno != ESRCH) {
        fprintf(stderr, "Could not send SIGTERM to test process group: %s\n",
                strerror(errno));
    }

    for (unsigned int attempt = 0; attempt < 50; ++attempt) {
        int status;
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            report_test_result(test, status);
            test->pid = -1;
            test->test_id = 0;
            test->name = NULL;
            return;
        }
        if (result < 0 && errno == ECHILD) {
            test->pid = -1;
            test->test_id = 0;
            test->name = NULL;
            return;
        }
        if (result < 0 && errno != EINTR) {
            fprintf(stderr, "Could not wait for test process: %s\n",
                    strerror(errno));
            break;
        }
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 100000000};
        while (nanosleep(&pause, &pause) < 0 && errno == EINTR) {
        }
    }

    puts("Test is still running after 5 seconds.");
    puts("  Sending SIGKILL to the test process group.");
    if (kill(-child, SIGKILL) < 0 && errno != ESRCH) {
        fprintf(stderr, "Could not send SIGKILL to test process group: %s\n",
                strerror(errno));
    }
    int status;
    pid_t result;
    do {
        result = waitpid(child, &status, 0);
    } while (result < 0 && errno == EINTR);
    if (result == child) {
        report_test_result(test, status);
    } else {
        fprintf(stderr, "Could not reap test process: %s\n", strerror(errno));
    }
    test->pid = -1;
    test->test_id = 0;
    test->name = NULL;
}

static void print_manual_test_steps(unsigned int test_id)
{
    switch (test_id) {
    case 3:
        putchar('\n'); print_separator('-');
        puts("TEST 3 - HEALTHY BASELINE");
        print_wrapped("Leave stress programs closed and watch the dashboard for several samples.", 2);
        print_wrapped("Confirm CPU, memory, and root filesystem are below their warning thresholds and status is HEALTHY.", 2);
        print_wrapped("The warning LED should be off. Full criteria: docs/test-cases.md.", 2);
        print_separator('-');
        break;
    case 4:
        putchar('\n'); print_separator('-');
        puts("TEST 4 - RECOVERY AFTER STRESS");
        print_wrapped("Use test 1, then let its full sequence finish while watching the dashboard.", 2);
        print_wrapped("Observe CPU use fall and status return to HEALTHY once every available metric is below threshold.", 2);
        print_wrapped("The warning LED should turn off when status recovers. Results depend on thresholds and measurements.", 2);
        print_wrapped("Full criteria: docs/test-cases.md.", 2);
        print_separator('-');
        break;
    case 5:
        putchar('\n'); print_separator('-');
        puts("TEST 5 - FILESYSTEM VERIFICATION");
        print_wrapped("Run df -h / in another terminal and compare its Used/Size reading with the dashboard.", 2);
        print_wrapped("Do not create files or fill the SD card. Small differences are expected from rounding and timing.", 2);
        print_wrapped("Full criteria: docs/test-cases.md.", 2);
        print_separator('-');
        break;
    default:
        break;
    }
}

static bool handle_control_line(char *line, bool *test_menu_open,
                                RunningTest *running_test)
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
    } else if (strcmp(command, "t") == 0) {
        print_test_menu();
        *test_menu_open = true;
    } else if (strcmp(command, "q") == 0) {
        stop_requested = 1;
    } else if (*test_menu_open && command[0] >= '1' && command[0] <= '5' &&
               command[1] == '\0') {
        unsigned int test_id = (unsigned int)(command[0] - '0');
        *test_menu_open = false;
        if (test_id <= 2) {
            start_stress_test(test_id, running_test);
        } else {
            print_manual_test_steps(test_id);
        }
    } else if (command[0] != '\0') {
        printf("Unknown command: %s\n", command);
        print_control_help();
    }
    return !stop_requested;
}

static bool timespec_is_at_or_after(const struct timespec *left,
                                    const struct timespec *right)
{
    return left->tv_sec > right->tv_sec ||
           (left->tv_sec == right->tv_sec &&
            left->tv_nsec >= right->tv_nsec);
}

/* Keep sampling on an absolute schedule and skip missed ticks instead of
 * adding the work time to every interval. */
static int advance_sample_deadline(struct timespec *deadline,
                                  unsigned int interval_seconds)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return -1;
    }
    do {
        deadline->tv_sec += (time_t)interval_seconds;
    } while (timespec_is_at_or_after(&now, deadline));
    return 0;
}

/* Poll stdin until the next sample deadline; completed terminal lines are
 * available after Enter when the terminal is in its normal canonical mode. */
static int wait_for_input_until(const struct timespec *deadline,
                                bool *input_open,
                                pid_t *browser_child,
                                bool *test_menu_open,
                                RunningTest *running_test)
{
    char line[128];
    size_t line_length = 0;
    bool discard_line = false;
    while (!stop_requested) {
        reap_browser(browser_child);
        reap_test(running_test);
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
            return -1;
        }
        time_t remaining_seconds = deadline->tv_sec - now.tv_sec;
        long remaining_nanoseconds = deadline->tv_nsec - now.tv_nsec;
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
        /* Check child completion regularly without adding another thread. */
        if (timeout_ms > 500) {
            timeout_ms = 500;
        }

        struct pollfd input = {
            .fd = input_open != NULL && *input_open ? STDIN_FILENO : -1,
            .events = POLLIN,
            .revents = 0
        };
        int poll_result = poll(&input, 1, (int)timeout_ms);
        if (poll_result == 0) {
            /* A 500 ms child check is not a sampling deadline. */
            continue;
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
                            if (!handle_control_line(line, test_menu_open,
                                                     running_test)) {
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
    bool test_menu_open = false;
    RunningTest running_test = {.pid = -1, .test_id = 0, .name = NULL};

    CpuSampleState cpu_state = {0};
    struct timespec next_sample_deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &next_sample_deadline) < 0) {
        perror("Could not set monitoring deadline");
        operation_failed = true;
        stop_requested = 1;
    }
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
            advance_sample_deadline(&next_sample_deadline,
                                    interval_seconds) < 0) {
            if (!stop_requested) {
                perror("Could not advance monitoring deadline");
                operation_failed = true;
            }
            break;
        }
        if (wait_for_input_until(&next_sample_deadline, &input_open,
                                 &browser_child, &test_menu_open,
                                 &running_test) < 0) {
            if (!stop_requested) {
                perror("Monitoring interval wait failed");
                operation_failed = true;
            }
            break;
        }
    }

    stop_running_test(&running_test);
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
