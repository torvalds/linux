# SPDX-License-Identifier: GPL-2.0

MOUNT_CFLAGS := $(shell pkg-config --cflags mount 2>/dev/null)
MOUNT_LDLIBS := $(shell pkg-config --libs mount 2>/dev/null)
ifeq ($(MOUNT_LDLIBS),)
MOUNT_LDLIBS := -lmount -I/usr/include/libmount
endif

CFLAGS += -O2 -g -std=gnu99 -Wall -I../../../../usr/include/ $(MOUNT_CFLAGS)
LDLIBS += $(MOUNT_LDLIBS)

TEST_PROGS := gpio-mockup.sh
TEST_FILES := gpio-mockup-sysfs.sh
TEST_PROGS_EXTENDED := gpio-mockup-chardev

GPIODIR := $(realpath ../../../gpio)
GPIOOBJ := gpio-utils.o

include ../lib.mk

all: $(TEST_PROGS_EXTENDED)

override define CLEAN
	$(RM) $(TEST_PROGS_EXTENDED)
	$(MAKE) -C $(GPIODIR) OUTPUT=$(GPIODIR)/ clean
endef

$(TEST_PROGS_EXTENDED):| khdr
$(TEST_PROGS_EXTENDED): $(GPIODIR)/$(GPIOOBJ)

$(GPIODIR)/$(GPIOOBJ):
	$(MAKE) OUTPUT=$(GPIODIR)/ -C $(GPIODIR)
