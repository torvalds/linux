# SPDX-License-Identifier: GPL-2.0
CC		= $(CROSS_COMPILE)gcc
BUILD_OUTPUT	:= $(CURDIR)
PREFIX		?= /usr
DESTDIR		?=

ifeq ("$(origin O)", "command line")
	BUILD_OUTPUT := $(O)
endif

turbostat : turbostat.c
override CFLAGS +=	-O2 -Wall -I../../../include
override CFLAGS +=	-DMSRHEADER='"../../../../arch/x86/include/asm/msr-index.h"'
override CFLAGS +=	-DINTEL_FAMILY_HEADER='"../../../../arch/x86/include/asm/intel-family.h"'
override CFLAGS +=	-D_FORTIFY_SOURCE=2

%: %.c
	@mkdir -p $(BUILD_OUTPUT)
	$(CC) $(CFLAGS) $< -o $(BUILD_OUTPUT)/$@ $(LDFLAGS)

.PHONY : clean
clean :
	@rm -f $(BUILD_OUTPUT)/turbostat

install : turbostat
	install -d  $(DESTDIR)$(PREFIX)/bin
	install $(BUILD_OUTPUT)/turbostat $(DESTDIR)$(PREFIX)/bin/turbostat
	install -d  $(DESTDIR)$(PREFIX)/share/man/man8
	install -m 644 turbostat.8 $(DESTDIR)$(PREFIX)/share/man/man8
