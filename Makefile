
CC=gcc
CFLAGS_COMMON=-Wall -Wextra -Wpedantic
CFLAGS_STRICT=-Werror -Wcast-align -Wcast-qual \
    -Wstrict-prototypes \
    -Wold-style-definition \
    -Wcast-align -Wcast-qual -Wconversion \
    -Wfloat-equal -Wformat=2 -Wformat-security \
    -Winit-self -Wjump-misses-init \
    -Wlogical-op -Wmissing-include-dirs \
    -Wnested-externs -Wpointer-arith \
    -Wredundant-decls -Wshadow \
    -Wstrict-overflow=5 -Wswitch-default \
    -Wswitch-enum -Wundef \
    -Wunreachable-code -Wunused \
    -Wwrite-strings
CFLAGS=$(CFLAGS_COMMON) $(CFLAGS_STRICT) -O3 -fstack-protector-strong
LDFLAGS=-lgps -lm

TARGET=gpsd_averaged
SOURCES=gpsd_averaged.c

##

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

format:
	clang-format -i $(SOURCES)

.PHONY: clean format

##

INSTALL_DIR = /usr/local/bin
DEFAULT_DIR = /etc/default
SYSTEMD_DIR = /etc/systemd/system
define install_service_systemd
	-systemctl stop $(1) 2>/dev/null || true
	-systemctl disable $(1) 2>/dev/null || true
	cp $(2).service $(SYSTEMD_DIR)/$(1).service
	systemctl daemon-reload
	systemctl enable $(1)
	systemctl start $(1) || echo "Warning: Failed to start $(1)"
endef
install_target: $(TARGET)
	install -m 755 $(TARGET) $(INSTALL_DIR)
install_default: $(TARGET).default
	cp $(TARGET).default $(DEFAULT_DIR)/$(TARGET)
install_service: $(TARGET).service
	$(call install_service_systemd,$(TARGET),$(TARGET))
install: install_target install_default install_service
restart:
	systemctl restart $(TARGET)
.PHONY: install install_target install_default install_service
.PHONY: restart

