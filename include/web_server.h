#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "health_monitor.h"

struct mg_context;

typedef struct {
    struct mg_context *context;
    pthread_mutex_t report_mutex;
    HealthReport latest_report;
    char sampled_at[32];
    char history_path[512];
    bool mutex_initialized;
    bool has_report;
    bool library_initialized;
} WebServer;

int web_server_start(WebServer *server, const char *host, unsigned int port,
                     const char *document_root, const char *history_path, char *error,
                     size_t error_size);
void web_server_update(WebServer *server, const HealthReport *report,
                       const char *timestamp_utc);
void web_server_stop(WebServer *server);

#endif
