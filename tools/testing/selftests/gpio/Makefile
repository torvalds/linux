# SPDX-License-Identifier: GPL-2.0

CFLAGS += -O2 -g -std=gnu99 -Wall -I../../../../usr/include/
LDLIBS += -lmount -I/usr/include/libmount

TEST_PROGS := gpio-mockup.sh
TEST_FILES := gpio-mockup-sysfs.sh
TEST_PROGS_EXTENDED := gpio-mockup-chardev

GPIODIR := $(realpath ../../../gpio)
GPIOOBJ := gpio-utils.o

all: $(TEST_PROGS_EXTENDED)

override define CLEAN
	$(RM) $(TEST_PROGS_EXTENDED)
	$(MAKE) -C $(GPIODIR) OUTPUT=$(GPIODIR)/ clean
endef

KSFT_KHDR_INSTALL := 1
include ../lib.mk

$(TEST_PROGS_EXTENDED): $(GPIODIR)/$(GPIOOBJ)

$(GPIODIR)/$(GPIOOBJ):
	$(MAKE) OUTPUT=$(GPIODIR)/ -C $(GPIODIR)
