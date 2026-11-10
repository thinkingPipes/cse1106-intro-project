#define _POSIX_C_SOURCE 200809L

#include "gpio_indicator.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBGPIOD
#include <gpiod.h>
#endif

#define WARNING_LED_GPIO 17U
#define DEFAULT_GPIO_CHIP "/dev/gpiochip0"


static void set_error(char *error, size_t error_size, const char *operation)
{
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s: %s", operation,
                       strerror(errno));
    }
}

int gpio_indicator_init(GpioIndicator *indicator, char *error,
                        size_t error_size)
{
    
    memset(indicator, 0, sizeof(*indicator));

#ifndef HAVE_LIBGPIOD
    (void)error;
    (void)error_size;
    errno = ENOSYS;
    set_error(error, error_size,
              "GPIO support was not built (install libgpiod development files and rebuild)");
    return -1;
#else
    
    const char *chip_path = getenv("HEALTH_MONITOR_GPIO_CHIP");
    if (chip_path == NULL || chip_path[0] == '\0') {
        chip_path = DEFAULT_GPIO_CHIP;
    }

    struct gpiod_chip *chip = gpiod_chip_open(chip_path);
    if (chip == NULL) {
        set_error(error, error_size, "could not open GPIO chip");
        return -1;
    }
    indicator->chip = chip;
    indicator->backend = GPIO_BACKEND_HARDWARE;

#ifdef GPIOD_API_V2
    
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *line_config = gpiod_line_config_new();
    struct gpiod_request_config *request_config = gpiod_request_config_new();
    if (settings == NULL || line_config == NULL || request_config == NULL) {
        set_error(error, error_size, "could not allocate GPIO configuration");
        gpiod_line_settings_free(settings);
        gpiod_line_config_free(line_config);
        gpiod_request_config_free(request_config);
        (void)gpio_indicator_close(indicator, NULL, 0);
        return -1;
    }

    unsigned int offset = WARNING_LED_GPIO;
    int config_result =
        gpiod_line_settings_set_direction(settings,
                                          GPIOD_LINE_DIRECTION_OUTPUT);
    if (config_result == 0) {
        config_result = gpiod_line_settings_set_output_value(
            settings, GPIOD_LINE_VALUE_INACTIVE);
    }
    if (config_result == 0) {
        config_result = gpiod_line_config_add_line_settings(
            line_config, &offset, 1, settings);
    }
    gpiod_request_config_set_consumer(request_config,
                                      "health-monitor-warning-led");
    struct gpiod_line_request *request = NULL;
    if (config_result == 0) {
        request = gpiod_chip_request_lines(chip, request_config, line_config);
    }
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_config);
    gpiod_request_config_free(request_config);
    if (request == NULL) {
        set_error(error, error_size, "could not request GPIO 17 as output");
        (void)gpio_indicator_close(indicator, NULL, 0);
        return -1;
    }
    indicator->line = request;
#else
    
    struct gpiod_line *line = gpiod_chip_get_line(chip, WARNING_LED_GPIO);
    if (line == NULL) {
        set_error(error, error_size, "could not find GPIO 17 on GPIO chip");
        (void)gpio_indicator_close(indicator, NULL, 0);
        return -1;
    }
    indicator->line = line;
    if (gpiod_line_request_output(line, "health-monitor-warning-led", 0) < 0) {
        set_error(error, error_size, "could not request GPIO 17 as output");
        (void)gpio_indicator_close(indicator, NULL, 0);
        return -1;
    }
#endif

    indicator->initialized = true;
    indicator->backend = GPIO_BACKEND_HARDWARE;
    return 0;
#endif
}

#ifdef HEALTH_MONITOR_TEST
int gpio_indicator_init_mock(GpioIndicator *indicator, char *error,
                             size_t error_size)
{
    
    (void)error;
    (void)error_size;
    memset(indicator, 0, sizeof(*indicator));
    indicator->backend = GPIO_BACKEND_MOCK;
    indicator->initialized = true;
    return gpio_indicator_set(indicator, false, error, error_size);
}
#endif

int gpio_indicator_set(GpioIndicator *indicator, bool on,
                       char *error, size_t error_size)
{
    
    if (indicator == NULL || !indicator->initialized) {
        errno = ENODEV;
        set_error(error, error_size, "GPIO indicator is not initialized");
        return -1;
    }

#ifdef HEALTH_MONITOR_TEST
    if (indicator->backend == GPIO_BACKEND_MOCK) {
        indicator->mock_output = on;
        if (on) {
            ++indicator->mock_high_requests;
        } else {
            ++indicator->mock_low_requests;
        }
        printf("MOCK GPIO 17: %s\n", on ? "HIGH" : "LOW");
        return 0;
    }
#endif

#ifndef HAVE_LIBGPIOD
    errno = ENOSYS;
    set_error(error, error_size, "GPIO support was not built");
    return -1;
#else
    if (indicator->line == NULL) {
        errno = ENODEV;
        set_error(error, error_size, "GPIO indicator is not initialized");
        return -1;
    }

#ifdef GPIOD_API_V2
    
    int result = gpiod_line_request_set_value(
        (struct gpiod_line_request *)indicator->line, WARNING_LED_GPIO,
        on ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
#else
    int result = gpiod_line_set_value((struct gpiod_line *)indicator->line,
                                      on ? 1 : 0);
#endif
    if (result < 0) {
        set_error(error, error_size, on ? "could not turn warning LED on"
                                        : "could not turn warning LED off");
        return -1;
    }
    return 0;
#endif
}

int gpio_indicator_apply_status(GpioIndicator *indicator, HealthStatus status,
                                char *error, size_t error_size)
{
    
    bool on = status == HEALTH_WARNING;
    return gpio_indicator_set(indicator, on, error, error_size);
}

int gpio_indicator_close(GpioIndicator *indicator, char *error,
                         size_t error_size)
{
    
    int result = 0;
    if (indicator == NULL) {
        return 0;
    }

    if (indicator->initialized && gpio_indicator_set(indicator, false,
                                                     error, error_size) < 0) {
        result = -1;
    }
#ifdef HAVE_LIBGPIOD
    if (indicator->backend == GPIO_BACKEND_HARDWARE) {
#ifdef GPIOD_API_V2
        if (indicator->line != NULL) {
            gpiod_line_request_release(
                (struct gpiod_line_request *)indicator->line);
        }
#else
        if (indicator->initialized && indicator->line != NULL) {
            gpiod_line_release((struct gpiod_line *)indicator->line);
        }
#endif
        if (indicator->chip != NULL) {
            gpiod_chip_close((struct gpiod_chip *)indicator->chip);
        }
    }
#endif

    indicator->initialized = false;
    indicator->chip = NULL;
    indicator->line = NULL;
    indicator->backend = GPIO_BACKEND_NONE;
    return result;
}
