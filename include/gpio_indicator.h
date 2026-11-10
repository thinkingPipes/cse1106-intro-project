/* Guard protects the GPIO abstraction shared by main and tests. */
#ifndef GPIO_INDICATOR_H
#define GPIO_INDICATOR_H

#include <stdbool.h>
#include <stddef.h>

#include "health_monitor.h"

/* Backend records whether requests go to libgpiod or the in-memory simulator. */
typedef enum {
    GPIO_BACKEND_NONE,
    GPIO_BACKEND_HARDWARE,
    GPIO_BACKEND_MOCK
} GpioBackend;

/* Opaque chip/line handles hide libgpiod version details from callers.
 * Mock fields make requested output transitions observable in tests. */
typedef struct {
    void *chip;
    void *line;
    bool initialized;
    bool mock_output;
    unsigned int mock_high_requests;
    unsigned int mock_low_requests;
    GpioBackend backend;
} GpioIndicator;

/* The module interface reports failures as -1 and writes a bounded explanation. */
int gpio_indicator_init(GpioIndicator *indicator, char *error, size_t error_size);
int gpio_indicator_init_mock(GpioIndicator *indicator,
                             char *error, size_t error_size);
int gpio_indicator_set(GpioIndicator *indicator, bool on,
                       char *error, size_t error_size);
int gpio_indicator_apply_status(GpioIndicator *indicator, HealthStatus status,
                                char *error, size_t error_size);
int gpio_indicator_close(GpioIndicator *indicator,
                         char *error, size_t error_size);

#endif
