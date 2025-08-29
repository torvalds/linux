# SPDX-License-Identifier: GPL-2.0
# Makefile for KFuzzTest-Bridge
include ../scripts/Makefile.include

bindir ?= /usr/bin

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

MAKEFLAGS += -r

override CFLAGS += -O2 -g
override CFLAGS += -Wall -Wextra
override CFLAGS += -D_GNU_SOURCE
override CFLAGS += -I$(OUTPUT)include -I$(srctree)/tools/include

ALL_TARGETS := kfuzztest-bridge
ALL_PROGRAMS := $(patsubst %,$(OUTPUT)%,$(ALL_TARGETS))

KFUZZTEST_BRIDGE_IN := $(OUTPUT)kfuzztest-bridge-in.o
KFUZZTEST_BRIDGE    := $(OUTPUT)kfuzztest-bridge

all: $(ALL_PROGRAMS)

export srctree OUTPUT CC LD CFLAGS
include $(srctree)/tools/build/Makefile.include

$(KFUZZTEST_BRIDGE_IN): FORCE
	$(Q)$(MAKE) $(build)=kfuzztest-bridge

$(KFUZZTEST_BRIDGE): $(KFUZZTEST_BRIDGE_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(ALL_PROGRAMS)
	find $(or $(OUTPUT),.) -name '*.o' -delete -o -name '\.*.d' -delete -o -name '\.*.o.cmd' -delete

install: $(ALL_PROGRAMS)
	install -d -m 755 $(DESTDIR)$(bindir);		\
	for program in $(ALL_PROGRAMS); do		\
		install $$program $(DESTDIR)$(bindir);	\
	done

FORCE:

.PHONY: all install clean FORCE prepare
