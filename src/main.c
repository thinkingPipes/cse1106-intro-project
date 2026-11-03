#include "health_monitor.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const char *program)
{
    printf("Usage: %s [--log FILE] [--help]\n", program);
    puts("Report Linux equipment health metrics.");
    puts("  --log FILE  Append this report to a CSV file");
    puts("  --help      Show this help message");
}

int main(int argc, char **argv)
{
    const char *log_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
            continue;
        }

        fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
        print_usage(argv[0]);
        return 2;
    }

    HealthReport report;
    collect_health_report(&report);
    print_health_report(&report);

    if (log_path != NULL && append_health_report(log_path, &report) != 0) {
        fprintf(stderr, "Could not append report to %s\n", log_path);
        return 1;
    }

    return 0;
}
