# SPDX-License-Identifier: GPL-2.0
# Makefile for thermal tools

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
# $(info Determined 'srctree' to be $(srctree))
endif

CFLAGS = -Wall -Wextra
CFLAGS += -I$(srctree)/tools/thermal/lib
CFLAGS += -I$(srctree)/tools/lib/thermal/include

LDFLAGS = -L$(srctree)/tools/thermal/lib
LDFLAGS += -L$(srctree)/tools/lib/thermal
LDFLAGS += -lthermal_tools
LDFLAGS += -lthermal
LDFLAGS += -lconfig
LDFLAGS += -lnl-genl-3 -lnl-3

VERSION = 0.0.1

all: thermal-engine
%: %.c
	$(CC) $(CFLAGS) -D VERSION=\"$(VERSION)\" -o $@ $^ $(LDFLAGS)
clean:
	$(RM) thermal-engine
