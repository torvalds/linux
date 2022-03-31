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

override CFLAGS += -O2 -Wall -g -D_GNU_SOURCE -I$(OUTPUT)include

ALL_TARGETS := counter_example
ALL_PROGRAMS := $(patsubst %,$(OUTPUT)%,$(ALL_TARGETS))

all: $(ALL_PROGRAMS)

export srctree OUTPUT CC LD CFLAGS
include $(srctree)/tools/build/Makefile.include

#
# We need the following to be outside of kernel tree
#
$(OUTPUT)include/linux/counter.h: ../../include/uapi/linux/counter.h
	mkdir -p $(OUTPUT)include/linux 2>&1 || true
	ln -sf $(CURDIR)/../../include/uapi/linux/counter.h $@

prepare: $(OUTPUT)include/linux/counter.h

COUNTER_EXAMPLE := $(OUTPUT)counter_example.o
$(COUNTER_EXAMPLE): prepare FORCE
	$(Q)$(MAKE) $(build)=counter_example
$(OUTPUT)counter_example: $(COUNTER_EXAMPLE)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(ALL_PROGRAMS)
	rm -rf $(OUTPUT)include/linux/counter.h
	find $(or $(OUTPUT),.) -name '*.o' -delete -o -name '\.*.d' -delete

install: $(ALL_PROGRAMS)
	install -d -m 755 $(DESTDIR)$(bindir);		\
	for program in $(ALL_PROGRAMS); do		\
		install $$program $(DESTDIR)$(bindir);	\
	done

FORCE:

.PHONY: all install clean FORCE prepare
