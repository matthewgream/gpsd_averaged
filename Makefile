
CC = gcc
CFLAGS_COMMON = -Wall -Wextra -Wpedantic
CFLAGS_STRICT = -Werror -Wcast-align -Wcast-qual \
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
    -Wwrite-strings \
    -fstack-protector-strong 
CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_STRICT) -O3 -march=native -fstack-protector-strong
LDFLAGS = -lgps -lm

gpsd_averaged: gpsd_averaged.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f gpsd_averaged

format:
	clang-format -i *.c

install: gpsd_averaged
	install -m 755 gpsd_averaged /usr/local/sbin/

.PHONY: clean install
