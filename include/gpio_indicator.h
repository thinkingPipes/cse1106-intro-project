#ifndef GPIO_INDICATOR_H
#define GPIO_INDICATOR_H

#include <stdbool.h>
#include <stddef.h>

#include "health_monitor.h"

typedef enum {
    GPIO_BACKEND_NONE,
    GPIO_BACKEND_HARDWARE
#ifdef HEALTH_MONITOR_TEST
    , GPIO_BACKEND_MOCK
#endif
} GpioBackend;

typedef struct {
    void *chip;
    void *line;
    bool initialized;
#ifdef HEALTH_MONITOR_TEST
    bool mock_output;
    unsigned int mock_high_requests;
    unsigned int mock_low_requests;
#endif
    GpioBackend backend;
} GpioIndicator;

int gpio_indicator_init(GpioIndicator *indicator, char *error, size_t error_size);
#ifdef HEALTH_MONITOR_TEST
int gpio_indicator_init_mock(GpioIndicator *indicator,
                             char *error, size_t error_size);
#endif
int gpio_indicator_set(GpioIndicator *indicator, bool on,
                       char *error, size_t error_size);
int gpio_indicator_apply_status(GpioIndicator *indicator, HealthStatus status,
                                char *error, size_t error_size);
int gpio_indicator_close(GpioIndicator *indicator,
                         char *error, size_t error_size);

#endif
