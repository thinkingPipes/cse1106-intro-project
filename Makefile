CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c11 -O2
CPPFLAGS += -Iinclude
LDLIBS +=

GPIO ?= auto
ifeq ($(GPIO),auto)
  ifneq ($(shell $(PKG_CONFIG) --exists libgpiod 2>/dev/null && echo yes),)
    GPIO := 1
  else
    GPIO := 0
  endif
endif

ifeq ($(GPIO),1)
  ifeq ($(shell $(PKG_CONFIG) --exists libgpiod 2>/dev/null && echo yes),)
    $(error GPIO=1 requires libgpiod development files and pkg-config metadata)
  endif
  CPPFLAGS += -DHAVE_LIBGPIOD $(shell $(PKG_CONFIG) --cflags libgpiod)
  LDLIBS += $(shell $(PKG_CONFIG) --libs libgpiod)
  ifneq ($(shell $(PKG_CONFIG) --atleast-version=2.0 libgpiod 2>/dev/null && echo yes),)
    CPPFLAGS += -DGPIOD_API_V2
  endif
endif

SOURCES := src/main.c src/monitor.c src/gpio_indicator.c
TARGET := health-monitor
TEST_TARGET := tests/test_monitor

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(SOURCES) include/health_monitor.h include/gpio_indicator.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

.PHONY: test
test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): tests/test_monitor.c src/monitor.c src/gpio_indicator.c include/health_monitor.h include/gpio_indicator.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_monitor.c src/monitor.c src/gpio_indicator.c $(LDFLAGS) $(LDLIBS) -o $@

clean:
	rm -f $(TARGET) $(TEST_TARGET)
