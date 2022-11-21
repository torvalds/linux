# SPDX-License-Identifier: GPL-2.0
# Makefile for vm tools
#
VAR_CFLAGS := $(shell pkg-config --cflags libtracefs 2>/dev/null)
VAR_LDLIBS := $(shell pkg-config --libs libtracefs 2>/dev/null)

TARGETS = latency-collector
CFLAGS = -Wall -Wextra -g -O2 $(VAR_CFLAGS)
LDFLAGS = -lpthread $(VAR_LDLIBS)

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	$(RM) latency-collector

prefix ?= /usr/local
sbindir ?= ${prefix}/sbin

install: all
	install -d $(DESTDIR)$(sbindir)
	install -m 755 -p $(TARGETS) $(DESTDIR)$(sbindir)
