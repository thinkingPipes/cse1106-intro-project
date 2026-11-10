#define _POSIX_C_SOURCE 200809L

#include "display.h"

#include <curses.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MIN_TERMINAL_ROWS 22
#define MIN_TERMINAL_COLUMNS 72

enum {
    PAIR_HEALTHY = 1,
    PAIR_WARNING,
    PAIR_UNKNOWN,
    PAIR_TITLE
};

static int metric_color(const Display *display, bool available, double value,
                        unsigned int threshold)
{
    if (!available) {
        return display->has_colors ? COLOR_PAIR(PAIR_UNKNOWN) : A_DIM;
    }
    if (value > (double)threshold) {
        return display->has_colors ? COLOR_PAIR(PAIR_WARNING) : A_BOLD;
    }
    return display->has_colors ? COLOR_PAIR(PAIR_HEALTHY) : A_NORMAL;
}

static void draw_bar(const Display *display, int row, const char *label,
                     bool available, double value, unsigned int threshold)
{
    int bar_width = COLS - 37;
    if (bar_width > 50) {
        bar_width = 50;
    }
    if (bar_width < 10) {
        bar_width = 10;
    }

    mvprintw(row, 2, "%-7s", label);
    attron(metric_color(display, available, value, threshold));
    addch('[');
    int filled = 0;
    if (available) {
        double bounded = value < 0.0 ? 0.0 : value > 100.0 ? 100.0 : value;
        filled = (int)(bounded * bar_width / 100.0 + 0.5);
    }
    for (int i = 0; i < bar_width; ++i) {
        addch(available && i < filled ? '#' : '-');
    }
    addch(']');
    attroff(metric_color(display, available, value, threshold));
    if (available) {
        mvprintw(row, 12 + bar_width, "%5.1f%%  warn > %u%%", value,
                 threshold);
    } else {
        mvprintw(row, 12 + bar_width, "  N/A   warn > %u%%", threshold);
    }
}

static void draw_history(const Display *display, int rows, int columns)
{
    int label_row = 15;
    mvprintw(label_row, 2, "CPU HISTORY  recent samples (0-100%%), threshold %u%%",
             display->last_report.cpu_warning_threshold_pct);
    int graph_top = 16;
    int graph_height = rows - 18;
    if (graph_height > 6) {
        graph_height = 6;
    }
    int graph_width = columns - 8;
    if (graph_width > CPU_HISTORY_LENGTH) {
        graph_width = CPU_HISTORY_LENGTH;
    }
    if (graph_width < 1 || graph_height < 1) {
        return;
    }

    if (display->history_count == 0) {
        mvprintw(graph_top, 4, "Waiting for valid CPU samples...");
        return;
    }
    size_t visible = display->history_count;
    if (visible > (size_t)graph_width) {
        visible = (size_t)graph_width;
    }
    int first_column = columns - 4 - (int)visible;
    for (size_t i = 0; i < visible; ++i) {
        size_t index = (display->history_start +
                        display->history_count - visible + i) %
                       CPU_HISTORY_LENGTH;
        double value = display->cpu_history[index];
        int level = (int)(value * (graph_height - 1) / 100.0 + 0.5);
        int row = graph_top + graph_height - 1 - level;
        int column = first_column + (int)i;
        attron(metric_color(display, true, value,
                            display->last_report.cpu_warning_threshold_pct));
        mvaddch(row, column, '*');
        attroff(metric_color(display, true, value,
                             display->last_report.cpu_warning_threshold_pct));
    }
    mvprintw(graph_top + graph_height, 2, "100%%");
    mvprintw(graph_top + graph_height - 1, 2, "  0%%");
}

static void draw_compact(const Display *display, int rows, int columns)
{
    erase();
    if (rows > 0 && columns > 0) {
        attron(display->has_colors ? COLOR_PAIR(PAIR_WARNING) : A_BOLD);
        mvaddnstr(0, 0, "Terminal too small: resize to at least 72 columns x 22 rows.",
                  columns - 1);
        attroff(display->has_colors ? COLOR_PAIR(PAIR_WARNING) : A_BOLD);
    }
    if (rows > 2 && columns > 0 && display->has_report) {
        int status_width = columns - 10;
        mvaddnstr(2, 0, "Status: ", columns - 1);
        if (status_width > 0) {
            addnstr(health_status_name(display->last_report.status),
                    status_width);
        }
    }
    if (rows > 4 && columns > 0) {
        mvaddnstr(rows - 1, 0, "Monitoring continues; enlarge terminal to view metrics.",
                  columns - 1);
    }
    refresh();
}

static void render_dashboard(Display *display)
{
    int rows;
    int columns;
    getmaxyx(stdscr, rows, columns);
    if (rows < MIN_TERMINAL_ROWS || columns < MIN_TERMINAL_COLUMNS) {
        draw_compact(display, rows, columns);
        return;
    }

    const HealthReport *report = &display->last_report;
    erase();
    attron(display->has_colors ? COLOR_PAIR(PAIR_TITLE) | A_BOLD : A_BOLD);
    mvprintw(0, 2, "RASPBERRY PI SYSTEM HEALTH MONITOR");
    attroff(display->has_colors ? COLOR_PAIR(PAIR_TITLE) | A_BOLD : A_BOLD);
    mvprintw(0, columns - 29, "Updated: %s", display->last_updated);
    mvhline(1, 1, ACS_HLINE, columns - 2);

    int status_color = report->status == HEALTH_WARNING
                           ? PAIR_WARNING
                           : report->status == HEALTHY ? PAIR_HEALTHY
                                                       : PAIR_UNKNOWN;
    attron(display->has_colors ? COLOR_PAIR(status_color) | A_BOLD : A_BOLD);
    mvprintw(2, 2, "OVERALL HEALTH: %s", health_status_name(report->status));
    attroff(display->has_colors ? COLOR_PAIR(status_color) | A_BOLD : A_BOLD);

    mvhline(3, 1, ACS_HLINE, columns - 2);
    mvprintw(4, 2, "CPU");
    draw_bar(display, 5, "Usage", report->has_cpu, report->cpu_used_pct,
             report->cpu_warning_threshold_pct);

    mvhline(6, 1, ACS_HLINE, columns - 2);
    mvprintw(7, 2, "MEMORY");
    bool has_memory = report->has_memory && report->memory_total_kb > 0 &&
                      report->memory_available_kb <= report->memory_total_kb;
    double memory_pct = has_memory
                            ? 100.0 * (double)(report->memory_total_kb -
                                               report->memory_available_kb) /
                                  (double)report->memory_total_kb
                            : 0.0;
    draw_bar(display, 8, "Used", has_memory, memory_pct,
             report->warning_threshold_pct);

    mvhline(9, 1, ACS_HLINE, columns - 2);
    mvprintw(10, 2, "ROOT FILESYSTEM");
    bool has_disk = report->has_disk && report->disk_total_bytes > 0 &&
                    report->disk_available_bytes <= report->disk_total_bytes;
    double disk_pct = has_disk
                          ? 100.0 * (double)(report->disk_total_bytes -
                                             report->disk_available_bytes) /
                                (double)report->disk_total_bytes
                          : 0.0;
    draw_bar(display, 11, "Used", has_disk, disk_pct,
             report->warning_threshold_pct);

    mvhline(12, 1, ACS_HLINE, columns - 2);
    mvprintw(13, 2, "CPU TEMPERATURE");
    attron(A_DIM);
    mvprintw(13, 21, "N/A (temperature collection not enabled)");
    attroff(A_DIM);
    mvhline(14, 1, ACS_HLINE, columns - 2);
    draw_history(display, rows, columns);
    refresh();
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

int display_init(Display *display, char *error, size_t error_size)
{
    if (display == NULL) {
        errno = EINVAL;
        set_error(error, error_size, "invalid display context");
        return -1;
    }
    memset(display, 0, sizeof(*display));
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        set_error(error, error_size, "interactive terminal is unavailable");
        return -1;
    }
    if (initscr() == NULL) {
        set_error(error, error_size, "ncurses could not initialize the terminal");
        return -1;
    }
    display->initialized = true;
    if (cbreak() == ERR || noecho() == ERR) {
        set_error(error, error_size, "ncurses could not configure terminal input");
        display_close(display);
        return -1;
    }
    (void)keypad(stdscr, TRUE);
    (void)nodelay(stdscr, TRUE);
    (void)curs_set(0);
    if (has_colors() && start_color() != ERR) {
        (void)use_default_colors();
        display->has_colors =
            init_pair(PAIR_HEALTHY, COLOR_GREEN, -1) != ERR &&
            init_pair(PAIR_WARNING, COLOR_YELLOW, -1) != ERR &&
            init_pair(PAIR_UNKNOWN, COLOR_CYAN, -1) != ERR &&
            init_pair(PAIR_TITLE, COLOR_CYAN, -1) != ERR;
    }
    return 0;
}

void display_update(Display *display, const HealthReport *report)
{
    if (display == NULL || !display->initialized || report == NULL) {
        return;
    }
    display->last_report = *report;
    display->has_report = true;
    if (report->has_cpu) {
        size_t index = (display->history_start + display->history_count) %
                       CPU_HISTORY_LENGTH;
        if (display->history_count == CPU_HISTORY_LENGTH) {
            index = display->history_start;
            display->history_start =
                (display->history_start + 1) % CPU_HISTORY_LENGTH;
        } else {
            ++display->history_count;
        }
        display->cpu_history[index] = report->cpu_used_pct;
    }
    time_t now = time(NULL);
    struct tm local_time;
    if (now != (time_t)-1 && localtime_r(&now, &local_time) != NULL &&
        strftime(display->last_updated, sizeof(display->last_updated),
                 "%Y-%m-%d %H:%M:%S", &local_time) > 0) {
        render_dashboard(display);
    } else {
        (void)snprintf(display->last_updated, sizeof(display->last_updated),
                       "unavailable");
        render_dashboard(display);
    }
}

void display_poll_events(Display *display)
{
    if (display == NULL || !display->initialized) {
        return;
    }
    int key;
    while ((key = getch()) != ERR) {
        if (key == KEY_RESIZE && display->has_report) {
            render_dashboard(display);
        }
    }
}

void display_close(Display *display)
{
    if (display != NULL && display->initialized) {
        (void)endwin();
        display->initialized = false;
    }
}
