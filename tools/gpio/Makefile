# SPDX-License-Identifier: GPL-2.0
include ../scripts/Makefile.include

bindir ?= /usr/bin

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

# Do not use make's built-in rules
# (this improves performance and avoids hard-to-debug behaviour);
MAKEFLAGS += -r

CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
CFLAGS += -O2 -Wall -g -D_GNU_SOURCE -I$(OUTPUT)include

ALL_TARGETS := lsgpio gpio-hammer gpio-event-mon
ALL_PROGRAMS := $(patsubst %,$(OUTPUT)%,$(ALL_TARGETS))

all: $(ALL_PROGRAMS)

export srctree OUTPUT CC LD CFLAGS
include $(srctree)/tools/build/Makefile.include

#
# We need the following to be outside of kernel tree
#
$(OUTPUT)include/linux/gpio.h: ../../include/uapi/linux/gpio.h
	mkdir -p $(OUTPUT)include/linux 2>&1 || true
	ln -sf $(CURDIR)/../../include/uapi/linux/gpio.h $@

prepare: $(OUTPUT)include/linux/gpio.h

#
# lsgpio
#
LSGPIO_IN := $(OUTPUT)lsgpio-in.o
$(LSGPIO_IN): prepare FORCE
	$(Q)$(MAKE) $(build)=lsgpio
$(OUTPUT)lsgpio: $(LSGPIO_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

#
# gpio-hammer
#
GPIO_HAMMER_IN := $(OUTPUT)gpio-hammer-in.o
$(GPIO_HAMMER_IN): prepare FORCE
	$(Q)$(MAKE) $(build)=gpio-hammer
$(OUTPUT)gpio-hammer: $(GPIO_HAMMER_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

#
# gpio-event-mon
#
GPIO_EVENT_MON_IN := $(OUTPUT)gpio-event-mon-in.o
$(GPIO_EVENT_MON_IN): prepare FORCE
	$(Q)$(MAKE) $(build)=gpio-event-mon
$(OUTPUT)gpio-event-mon: $(GPIO_EVENT_MON_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(ALL_PROGRAMS)
	rm -f $(OUTPUT)include/linux/gpio.h
	find $(if $(OUTPUT),$(OUTPUT),.) -name '*.o' -delete -o -name '\.*.d' -delete

install: $(ALL_PROGRAMS)
	install -d -m 755 $(DESTDIR)$(bindir);		\
	for program in $(ALL_PROGRAMS); do		\
		install $$program $(DESTDIR)$(bindir);	\
	done

FORCE:

.PHONY: all install clean FORCE prepare
