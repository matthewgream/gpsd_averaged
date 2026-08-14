
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
# Position source: NMEA reads the serial device directly (no gpsd, no libgps); GPSD is the libgps client.
GPS_SOURCE ?= NMEA
LIBS_NMEA=-lm
LIBS_GPSD=-lgps -lm
CFLAGS=$(CFLAGS_COMMON) $(CFLAGS_STRICT) -O3 -fstack-protector-strong -DGPS_SOURCE_$(GPS_SOURCE)
LDFLAGS=$(LIBS_$(GPS_SOURCE))

TARGET=gpsd_averaged
SOURCES=gpsd_averaged.c
HEADERS=gpsd_interface.h
# The gpsd build binds its unit to gpsd.service; the NMEA build must not, as gpsd is not involved.
SVC_SRC:=$(if $(filter GPSD,$(GPS_SOURCE)),$(TARGET).gpsd,$(TARGET))
HOSTNAME:=$(shell hostname)
CFG_SRC:=$(if $(wildcard $(TARGET).$(HOSTNAME).cfg),$(TARGET).$(HOSTNAME).cfg,$(TARGET).cfg)

##

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET) $(TARGET).armhf

format:
	clang-format-19 -i $(SOURCES) $(HEADERS)

DEV_PACKAGES_NMEA=
DEV_PACKAGES_GPSD=libgps-dev
DEV_PACKAGES=$(DEV_PACKAGES_$(GPS_SOURCE))
DEV_PACKAGES_ARMHF=$(addsuffix :armhf,$(DEV_PACKAGES))
install-dev:
ifeq ($(strip $(DEV_PACKAGES)),)
	@echo "no dev packages required for GPS_SOURCE=$(GPS_SOURCE)"
else
	apt install -y $(DEV_PACKAGES)
endif
remove-dev:
ifeq ($(strip $(DEV_PACKAGES)),)
	@echo "no dev packages required for GPS_SOURCE=$(GPS_SOURCE)"
else
	apt purge -y $(DEV_PACKAGES)
endif
install-dev-armhf:
	dpkg --add-architecture armhf
	apt update
	apt install -y gcc-arm-linux-gnueabihf $(DEV_PACKAGES_ARMHF)
remove-dev-armhf:
	apt purge -y gcc-arm-linux-gnueabihf $(DEV_PACKAGES_ARMHF)
	dpkg --remove-architecture armhf
	apt update

CROSS_CC_ARMHF=arm-linux-gnueabihf-gcc
$(TARGET).armhf: $(SOURCES) $(HEADERS)
	$(CROSS_CC_ARMHF) $(CFLAGS) -o $(TARGET).armhf $< $(LDFLAGS)
armhf: $(TARGET).armhf

.PHONY: all clean format install-dev remove-dev install-dev-armhf remove-dev-armhf armhf

##

INSTALL=gpsd_averaged
DIR_INSTALL=/usr/local/bin
DIR_DEFAULT=/etc/default
DIR_SYSTEMD=/etc/systemd/system
DIR_AVAHI=/etc/avahi/services
DIR_UDEV=/etc/udev/rules.d
define install_service_systemd
	-systemctl stop $(2) 2>/dev/null || true
	-systemctl disable $(2) 2>/dev/null || true
	install -m 644 $(1).service $(DIR_SYSTEMD)/$(2).service
	systemctl daemon-reload
	systemctl enable $(2)
	systemctl start $(2) || echo "Warning: Failed to start $(2)"
endef
define install_service_avahi
	install -m 644 $(1).service $(DIR_AVAHI)/$(2).service
	systemctl restart avahi-daemon
endef
define install_rules_udev
	install -m 644 $(1).rules $(DIR_UDEV)/$(2).rules
	udevadm control --reload
	udevadm trigger
endef
install_target: $(TARGET)
	install -m 755 $(TARGET) $(DIR_INSTALL)/$(INSTALL)
install_default: $(CFG_SRC)
	@echo "installing config from $(CFG_SRC)"
	install -m 644 $(CFG_SRC) $(DIR_DEFAULT)/$(INSTALL)
install_service: $(SVC_SRC).service
	@echo "installing service from $(SVC_SRC).service"
	$(call install_service_systemd,$(SVC_SRC),$(INSTALL))
install_avahi_service_gps: config/avahi-gps.service
	$(call install_service_avahi,config/avahi-gps,gpsd-gps)
install_avahi_service_ntp: config/avahi-ntp.service
	$(call install_service_avahi,config/avahi-ntp,gpsd-ntp)
install_udev_rules: config/99-gps.rules
	$(call install_rules_udev,config/99-gps,99-gps)
install_avahi_service: install_avahi_service_gps install_avahi_service_ntp
install: install_target install_default install_service
restart:
	systemctl restart $(INSTALL)
.PHONY: install install_target install_default install_service
.PHONY: install_avahi_service install_avahi_service_gps install_avahi_service_ntp install_udev_rules
.PHONY: restart

##
