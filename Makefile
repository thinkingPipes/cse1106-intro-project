CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c11 -O2
CPPFLAGS += -Iinclude

GPIOD_CFLAGS := $(shell $(PKG_CONFIG) --cflags libgpiod 2>/dev/null)
GPIOD_LIBS := $(shell $(PKG_CONFIG) --libs libgpiod 2>/dev/null)
CIVETWEB_CFLAGS := $(shell $(PKG_CONFIG) --cflags civetweb 2>/dev/null)
CIVETWEB_LIBS := $(shell $(PKG_CONFIG) --libs civetweb 2>/dev/null)
ifneq ($(shell $(PKG_CONFIG) --atleast-version=2.0 libgpiod 2>/dev/null && echo yes),)
  GPIOD_CPPFLAGS := -DGPIOD_API_V2
endif

SOURCES := src/main.c src/monitor.c src/gpio_indicator.c src/web_server.c src/web_api.c
TARGET := .build/health-monitor
TEST_TARGET := .build/test_monitor
API_TEST_TARGET := .build/test_web_api
MOCK_SERVER_TARGET := .build/health-monitor-mock

.PHONY: all
all: $(TARGET)

$(TARGET): $(SOURCES) include/health_monitor.h include/gpio_indicator.h include/web_server.h include/web_api.h | .build
	@$(PKG_CONFIG) --exists libgpiod || { echo "libgpiod development files are required for GPIO 17 support" >&2; exit 1; }
	@$(PKG_CONFIG) --exists civetweb || { echo "CivetWeb development files are required for the HTTP server" >&2; exit 1; }
	$(CC) $(CPPFLAGS) -DHAVE_LIBGPIOD $(GPIOD_CPPFLAGS) $(GPIOD_CFLAGS) $(CIVETWEB_CFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(GPIOD_LIBS) $(CIVETWEB_LIBS) -pthread -o $@

.PHONY: test
test: $(TEST_TARGET) $(API_TEST_TARGET)
	$(TEST_TARGET)
	$(API_TEST_TARGET)

$(TEST_TARGET): tests/test_monitor.c src/monitor.c src/gpio_indicator.c include/health_monitor.h include/gpio_indicator.h | .build
	$(CC) $(CPPFLAGS) -DHEALTH_MONITOR_TEST $(CFLAGS) tests/test_monitor.c src/monitor.c src/gpio_indicator.c $(LDFLAGS) -o $@

$(API_TEST_TARGET): tests/test_web_api.c src/web_api.c src/monitor.c include/health_monitor.h include/web_api.h | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_web_api.c src/web_api.c src/monitor.c $(LDFLAGS) -o $@

.build:
	mkdir -p .build

.PHONY: integration-build
integration-build: $(MOCK_SERVER_TARGET)

$(MOCK_SERVER_TARGET): $(SOURCES) include/health_monitor.h include/gpio_indicator.h include/web_server.h include/web_api.h | .build
	$(CC) $(CPPFLAGS) -DHEALTH_MONITOR_TEST $(CIVETWEB_CFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(CIVETWEB_LIBS) -pthread -o $@

.PHONY: clean
clean:
	rm -f $(TARGET) $(TEST_TARGET) $(API_TEST_TARGET) $(MOCK_SERVER_TARGET)
