# SPDX-License-Identifier: GPL-2.0
# Makefile for cgroup tools

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
# $(info Determined 'srctree' to be $(srctree))
endif

CFLAGS = -Wall -Wextra
CFLAGS += -I$(srctree)/tools/thermal/lib

LDFLAGS = -L$(srctree)/tools/thermal/lib
LDFLAGS += -lthermal_tools
LDFLAGS += -lconfig

VERSION = 0.0.1
TARGET=thermometer

all: $(TARGET)
%: %.c
	$(CC) $(CFLAGS) -D VERSION=\"$(VERSION)\" -o $@ $^ $(LDFLAGS)

clean:
	$(RM) $(TARGET)
