#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stddef.h>

#include "health_monitor.h"

#define CPU_HISTORY_LENGTH 60

typedef struct {
    double cpu_history[CPU_HISTORY_LENGTH];
    size_t history_start;
    size_t history_count;
    HealthReport last_report;
    char last_updated[32];
    bool has_report;
    bool initialized;
    bool has_colors;
} Display;

int display_init(Display *display, char *error, size_t error_size);
void display_update(Display *display, const HealthReport *report);
void display_poll_events(Display *display);
void display_close(Display *display);

#endif
