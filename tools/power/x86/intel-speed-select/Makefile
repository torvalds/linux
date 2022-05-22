# SPDX-License-Identifier: GPL-2.0
include ../../../scripts/Makefile.include

bindir ?= /usr/bin

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

# Do not use make's built-in rules
# (this improves performance and avoids hard-to-debug behaviour);
MAKEFLAGS += -r
override CFLAGS += -O2 -Wall -g -D_GNU_SOURCE -I$(OUTPUT)include -I/usr/include/libnl3
override LDFLAGS += -lnl-genl-3 -lnl-3

ALL_TARGETS := intel-speed-select
ALL_PROGRAMS := $(patsubst %,$(OUTPUT)%,$(ALL_TARGETS))

all: $(ALL_PROGRAMS)

export srctree OUTPUT CC LD CFLAGS
include $(srctree)/tools/build/Makefile.include

#
# We need the following to be outside of kernel tree
#
$(OUTPUT)include/linux/isst_if.h: ../../../../include/uapi/linux/isst_if.h
	mkdir -p $(OUTPUT)include/linux 2>&1 || true
	ln -sf $(CURDIR)/../../../../include/uapi/linux/isst_if.h $@

$(OUTPUT)include/linux/thermal.h: ../../../../include/uapi/linux/thermal.h
	mkdir -p $(OUTPUT)include/linux 2>&1 || true
	ln -sf $(CURDIR)/../../../../include/uapi/linux/thermal.h $@

prepare: $(OUTPUT)include/linux/isst_if.h $(OUTPUT)include/linux/thermal.h

ISST_IN := $(OUTPUT)intel-speed-select-in.o

$(ISST_IN): prepare FORCE
	$(Q)$(MAKE) $(build)=intel-speed-select
$(OUTPUT)intel-speed-select: $(ISST_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

clean:
	rm -f $(ALL_PROGRAMS)
	rm -rf $(OUTPUT)include/linux/isst_if.h
	find $(or $(OUTPUT),.) -name '*.o' -delete -o -name '\.*.d' -delete

install: $(ALL_PROGRAMS)
	install -d -m 755 $(DESTDIR)$(bindir);		\
	for program in $(ALL_PROGRAMS); do		\
		install $$program $(DESTDIR)$(bindir);	\
	done

FORCE:

.PHONY: all install clean FORCE prepare
