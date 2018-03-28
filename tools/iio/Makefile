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

CFLAGS += -O2 -Wall -g -D_GNU_SOURCE -I$(OUTPUT)include

ALL_TARGETS := iio_event_monitor lsiio iio_generic_buffer
ALL_PROGRAMS := $(patsubst %,$(OUTPUT)%,$(ALL_TARGETS))

all: $(ALL_PROGRAMS)

export srctree OUTPUT CC LD CFLAGS
include $(srctree)/tools/build/Makefile.include

#
# We need the following to be outside of kernel tree
#
$(OUTPUT)include/linux/iio: ../../include/uapi/linux/iio
	mkdir -p $(OUTPUT)include/linux/iio 2>&1 || true
	ln -sf $(CURDIR)/../../include/uapi/linux/iio/events.h $@
	ln -sf $(CURDIR)/../../include/uapi/linux/iio/types.h $@

prepare: $(OUTPUT)include/linux/iio

LSIIO_IN := $(OUTPUT)lsiio-in.o
$(LSIIO_IN): prepare FORCE
	$(Q)$(MAKE) $(build)=lsiio
$(OUTPUT)lsiio: $(LSIIO_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

IIO_EVENT_MONITOR_IN := $(OUTPUT)iio_event_monitor-in.o
$(IIO_EVENT_MONITOR_IN): prepare FORCE
	$(Q)$(MAKE) $(build)=iio_event_monitor
$(OUTPUT)iio_event_monitor: $(IIO_EVENT_MONITOR_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

IIO_GENERIC_BUFFER_IN := $(OUTPUT)iio_generic_buffer-in.o
$(IIO_GENERIC_BUFFER_IN): prepare FORCE
	$(Q)$(MAKE) $(build)=iio_generic_buffer
$(OUTPUT)iio_generic_buffer: $(IIO_GENERIC_BUFFER_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(ALL_PROGRAMS)
	rm -rf $(OUTPUT)include/linux/iio
	find $(if $(OUTPUT),$(OUTPUT),.) -name '*.o' -delete -o -name '\.*.d' -delete

install: $(ALL_PROGRAMS)
	install -d -m 755 $(DESTDIR)$(bindir);		\
	for program in $(ALL_PROGRAMS); do		\
		install $$program $(DESTDIR)$(bindir);	\
	done

FORCE:

.PHONY: all install clean FORCE prepare
