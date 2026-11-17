#define _POSIX_C_SOURCE 200809L

#include "web_server.h"
#include "web_api.h"

#include <civetweb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int send_json(struct mg_connection *connection, const char *body,
                     int body_length)
{
    if (mg_send_http_ok(connection, "application/json; charset=utf-8",
                        (long long)body_length) < 0) {
        return 500;
    }
    if (mg_write(connection, body, (size_t)body_length) != body_length) {
        return 500;
    }
    return 200;
}

static int send_api_error(struct mg_connection *connection, int status,
                          const char *reason, const char *code,
                          const char *message)
{
    char body[256];
    int body_length = snprintf(body, sizeof(body),
                               "{\"error\":\"%s\",\"message\":\"%s\"}\n",
                               code, message);
    if (body_length < 0 || (size_t)body_length >= sizeof(body)) {
        return 500;
    }
    (void)mg_printf(connection,
                    "HTTP/1.1 %d %s\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Cache-Control: no-store\r\n"
                    "Content-Length: %d\r\n"
                    "Connection: close\r\n\r\n%s",
                    status, reason, body_length, body);
    return status;
}

static int serialize_snapshot(struct mg_connection *connection, void *data,
                              bool metrics)
{
    WebServer *server = data;
    const struct mg_request_info *request = mg_get_request_info(connection);
    const char *expected_uri = metrics ? "/api/metrics" : "/api/health";
    if (request == NULL || request->local_uri == NULL ||
        strcmp(request->local_uri, expected_uri) != 0) {
        return send_api_error(connection, 404, "Not Found", "not_found",
                              "API endpoint not found");
    }
    if (request->request_method == NULL ||
        strcmp(request->request_method, "GET") != 0) {
        return send_api_error(connection, 405, "Method Not Allowed",
                              "method_not_allowed", "Only GET is supported");
    }

    HealthReport report;
    char timestamp[sizeof(server->sampled_at)];
    bool has_report;
    (void)pthread_mutex_lock(&server->report_mutex);
    report = server->latest_report;
    has_report = server->has_report;
    (void)snprintf(timestamp, sizeof(timestamp), "%s", server->sampled_at);
    (void)pthread_mutex_unlock(&server->report_mutex);

    char body[1024];
    int length = metrics
                     ? web_api_metrics_json(&report,
                                            has_report && timestamp[0] != '\0'
                                                ? timestamp
                                                : NULL,
                                            body, sizeof(body))
                     : web_api_health_json(&report,
                                           has_report && timestamp[0] != '\0'
                                               ? timestamp
                                               : NULL,
                                           body, sizeof(body));
    if (length < 0) {
        return send_api_error(connection, 500, "Internal Server Error",
                              "serialization_failed",
                              "Could not serialize health report");
    }
    return send_json(connection, body, length);
}

static int health_handler(struct mg_connection *connection, void *data)
{
    return serialize_snapshot(connection, data, false);
}

static int metrics_handler(struct mg_connection *connection, void *data)
{
    return serialize_snapshot(connection, data, true);
}

static int history_handler(struct mg_connection *connection, void *data)
{
    WebServer *server = data;
    const struct mg_request_info *request = mg_get_request_info(connection);
    if (request == NULL || request->local_uri == NULL || strcmp(request->local_uri, "/api/history") != 0)
        return send_api_error(connection, 404, "Not Found", "not_found", "API endpoint not found");
    if (request->request_method == NULL || strcmp(request->request_method, "GET") != 0)
        return send_api_error(connection, 405, "Method Not Allowed", "method_not_allowed", "Only GET is supported");
    size_t limit;
    if (web_api_parse_history_limit(request->query_string, &limit) != 0)
        return send_api_error(connection, 400, "Bad Request", "invalid_limit", "limit must be an integer from 1 to 1000");
    HistorySample *samples = calloc(limit, sizeof(*samples));
    size_t capacity = 256 + limit * 192;
    char *body = malloc(capacity);
    if (samples == NULL || body == NULL) { free(samples); free(body); return send_api_error(connection, 500, "Internal Server Error", "history_unavailable", "Could not read history"); }
    size_t count = 0;
    if (web_api_read_history(server->history_path[0] ? server->history_path : NULL, limit, samples, &count) != 0) {
        free(samples); free(body); return send_api_error(connection, 500, "Internal Server Error", "history_unavailable", "Could not read history");
    }
    int length = web_api_history_json(samples, count, body, capacity);
    free(samples);
    if (length < 0) { free(body); return send_api_error(connection, 500, "Internal Server Error", "serialization_failed", "Could not serialize history"); }
    int result = send_json(connection, body, length); free(body); return result;
}

int web_server_start(WebServer *server, const char *host, unsigned int port,
                     const char *document_root, const char *history_path, char *error,
                     size_t error_size)
{
    if (server == NULL || host == NULL || host[0] == '\0' || port == 0 ||
        document_root == NULL || document_root[0] == '\0') {
        set_error(error, error_size, "invalid web server configuration");
        return -1;
    }
    memset(server, 0, sizeof(*server));
    if (history_path != NULL && strlen(history_path) >= sizeof(server->history_path)) {
        set_error(error, error_size, "history path is too long"); return -1;
    }
    if (history_path != NULL) (void)snprintf(server->history_path, sizeof(server->history_path), "%s", history_path);
    if (pthread_mutex_init(&server->report_mutex, NULL) != 0) {
        set_error(error, error_size, "could not initialize report mutex");
        return -1;
    }
    server->mutex_initialized = true;

    char listening_address[320];
    int address_length = snprintf(listening_address, sizeof(listening_address),
                                  "%s:%u", host, port);
    if (address_length < 0 || (size_t)address_length >= sizeof(listening_address)) {
        set_error(error, error_size, "host address is too long");
        web_server_stop(server);
        return -1;
    }

    char threads[] = "5";
    char listing[] = "no";
    const char *options[] = {"listening_ports", listening_address,
                             "document_root", document_root,
                             "num_threads", threads,
                             "enable_directory_listing", listing,
                             "cgi_pattern", "^$",
                             "request_timeout_ms", "5000", NULL};
    if (mg_init_library(MG_FEATURES_FILES) == 0) {
        set_error(error, error_size, "CivetWeb file serving is unavailable");
        web_server_stop(server);
        return -1;
    }
    server->library_initialized = true;
    server->context = mg_start(NULL, 0, options);
    if (server->context == NULL) {
        set_error(error, error_size, "CivetWeb could not bind the configured address");
        web_server_stop(server);
        return -1;
    }
    mg_set_request_handler(server->context, "/api/health", health_handler,
                           server);
    mg_set_request_handler(server->context, "/api/metrics", metrics_handler,
                           server);
    mg_set_request_handler(server->context, "/api/history", history_handler,
                           server);
    return 0;
}

void web_server_update(WebServer *server, const HealthReport *report,
                       const char *timestamp_utc)
{
    if (server == NULL || report == NULL || !server->mutex_initialized) {
        return;
    }
    (void)pthread_mutex_lock(&server->report_mutex);
    server->latest_report = *report;
    server->has_report = true;
    (void)snprintf(server->sampled_at, sizeof(server->sampled_at), "%s",
                   timestamp_utc != NULL ? timestamp_utc : "");
    (void)pthread_mutex_unlock(&server->report_mutex);
}

void web_server_stop(WebServer *server)
{
    if (server == NULL) {
        return;
    }
    if (server->context != NULL) {
        mg_stop(server->context);
        server->context = NULL;
    }
    if (server->library_initialized) {
        mg_exit_library();
        server->library_initialized = false;
    }
    if (server->mutex_initialized) {
        (void)pthread_mutex_destroy(&server->report_mutex);
        server->mutex_initialized = false;
    }
}
