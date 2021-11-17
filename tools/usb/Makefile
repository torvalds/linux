# SPDX-License-Identifier: GPL-2.0
# Makefile for USB tools
include ../scripts/Makefile.include

bindir ?= /usr/bin

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

# Do not use make's built-in rules
# (this improves performance and avoids hard-to-debug behaviour);
MAKEFLAGS += -r

override CFLAGS += -O2 -Wall -Wextra -g -D_GNU_SOURCE -I$(OUTPUT)include -I$(srctree)/tools/include
override LDFLAGS += -lpthread

ALL_TARGETS := testusb ffs-test
ALL_PROGRAMS := $(patsubst %,$(OUTPUT)%,$(ALL_TARGETS))

all: $(ALL_PROGRAMS)

export srctree OUTPUT CC LD CFLAGS
include $(srctree)/tools/build/Makefile.include

TESTUSB_IN := $(OUTPUT)testusb-in.o
$(TESTUSB_IN): FORCE
	$(Q)$(MAKE) $(build)=testusb
$(OUTPUT)testusb: $(TESTUSB_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

FFS_TEST_IN := $(OUTPUT)ffs-test-in.o
$(FFS_TEST_IN): FORCE
	$(Q)$(MAKE) $(build)=ffs-test
$(OUTPUT)ffs-test: $(FFS_TEST_IN)
	$(QUIET_LINK)$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(ALL_PROGRAMS)
	find $(if $(OUTPUT),$(OUTPUT),.) -name '*.o' -delete -o -name '\.*.d' -delete -o -name '\.*.o.cmd' -delete

install: $(ALL_PROGRAMS)
	install -d -m 755 $(DESTDIR)$(bindir);		\
	for program in $(ALL_PROGRAMS); do		\
		install $$program $(DESTDIR)$(bindir);	\
	done

FORCE:

.PHONY: all install clean FORCE prepare
