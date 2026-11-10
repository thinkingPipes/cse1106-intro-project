CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c11 -O2
CPPFLAGS += -Iinclude
LDLIBS +=

ifeq ($(shell $(PKG_CONFIG) --exists libgpiod 2>/dev/null && echo yes),)
  $(error libgpiod development files are required for the physical GPIO build)
endif
CPPFLAGS += -DHAVE_LIBGPIOD $(shell $(PKG_CONFIG) --cflags libgpiod)
LDLIBS += $(shell $(PKG_CONFIG) --libs libgpiod)
NCURSES_CFLAGS := $(shell $(PKG_CONFIG) --cflags ncurses 2>/dev/null)
NCURSES_LIBS := $(shell $(PKG_CONFIG) --libs ncurses 2>/dev/null)
ifneq ($(shell $(PKG_CONFIG) --atleast-version=2.0 libgpiod 2>/dev/null && echo yes),)
  CPPFLAGS += -DGPIOD_API_V2
endif

SOURCES := src/main.c src/monitor.c src/gpio_indicator.c
TARGET := health-monitor
TEST_TARGET := tests/test_monitor

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(SOURCES) src/display.c include/health_monitor.h include/gpio_indicator.h include/display.h
	@$(PKG_CONFIG) --exists ncurses || { echo "ncurses development files are required for the dashboard" >&2; exit 1; }
	$(CC) $(CPPFLAGS) $(NCURSES_CFLAGS) $(CFLAGS) $(SOURCES) src/display.c $(LDFLAGS) $(LDLIBS) $(NCURSES_LIBS) -o $@

.PHONY: test
test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): tests/test_monitor.c src/monitor.c src/gpio_indicator.c include/health_monitor.h include/gpio_indicator.h
	$(CC) $(CPPFLAGS) -DHEALTH_MONITOR_TEST $(CFLAGS) tests/test_monitor.c src/monitor.c src/gpio_indicator.c $(LDFLAGS) $(LDLIBS) -o $@

clean:
	rm -f $(TARGET) $(TEST_TARGET)
