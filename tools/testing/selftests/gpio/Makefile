# SPDX-License-Identifier: GPL-2.0

VAR_CFLAGS := $(shell pkg-config --cflags mount 2>/dev/null)
VAR_LDLIBS := $(shell pkg-config --libs mount 2>/dev/null)
ifeq ($(VAR_LDLIBS),)
VAR_LDLIBS := -lmount -I/usr/include/libmount
endif

CFLAGS += -O2 -g -std=gnu99 -Wall -I../../../../usr/include/ $(VAR_CFLAGS)
LDLIBS += $(VAR_LDLIBS)

TEST_PROGS := gpio-mockup.sh
TEST_FILES := gpio-mockup-sysfs.sh
TEST_GEN_PROGS_EXTENDED := gpio-mockup-chardev

KSFT_KHDR_INSTALL := 1
include ../lib.mk

GPIODIR := $(realpath ../../../gpio)
GPIOOUT := $(OUTPUT)/tools-gpio/
GPIOOBJ := $(GPIOOUT)/gpio-utils.o

CLEAN += ; $(RM) -rf $(GPIOOUT)

$(TEST_GEN_PROGS_EXTENDED): $(GPIOOBJ)

$(GPIOOUT):
	mkdir -p $@

$(GPIOOBJ): $(GPIOOUT)
	$(MAKE) OUTPUT=$(GPIOOUT) -C $(GPIODIR)
