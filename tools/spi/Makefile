# SPDX-License-Identifier: GPL-2.0-only
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

ALL_TARGETS := spidev_test spidev_fdx
ALL_PROGRAMS := $(patsubst %,$(OUTPUT)%,$(ALL_TARGETS))

all: $(ALL_PROGRAMS)

export srctree OUTPUT CC LD CFLAGS
include $(srctree)/tools/build/Makefile.include

#
# We need the following to be outside of kernel tree
#
$(OUTPUT)include/linux/spi/spidev.h: ../../include/uapi/linux/spi/spidev.h
	mkdir -p $(OUTPUT)include/linux/spi 2>&1 || true
	ln -sf $(CURDIR)/../../include/uapi/linux/spi/spidev.h $@

prepare: $(OUTPUT)include/linux/spi/spidev.h

#
# spidev_test
#
SPIDEV_TEST_IN := $(OUTPUT)spidev_test-in.o
$(SPIDEV_TEST_IN): prepare FORCE
	$(Q)$(MAKE) $(build)=spidev_test
$(OUTPUT)spidev_test: $(SPIDEV_TEST_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

#
# spidev_fdx
#
SPIDEV_FDX_IN := $(OUTPUT)spidev_fdx-in.o
$(SPIDEV_FDX_IN): prepare FORCE
	$(Q)$(MAKE) $(build)=spidev_fdx
$(OUTPUT)spidev_fdx: $(SPIDEV_FDX_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(ALL_PROGRAMS)
	rm -rf $(OUTPUT)include/
	find $(if $(OUTPUT),$(OUTPUT),.) -name '*.o' -delete -o -name '\.*.d' -delete

install: $(ALL_PROGRAMS)
	install -d -m 755 $(DESTDIR)$(bindir);		\
	for program in $(ALL_PROGRAMS); do		\
		install $$program $(DESTDIR)$(bindir);	\
	done

FORCE:

.PHONY: all install clean FORCE prepare
