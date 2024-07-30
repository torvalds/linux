# SPDX-License-Identifier: GPL-2.0
# Makefile for x86/kcpuid tool

kcpuid : kcpuid.c

CFLAGS = -Wextra

BINDIR ?= /usr/sbin

HWDATADIR ?= /usr/share/misc/

override CFLAGS += -O2 -Wall -I../../../include

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

.PHONY : clean
clean :
	@rm -f kcpuid

install : kcpuid
	install -d  $(DESTDIR)$(BINDIR) $(DESTDIR)$(HWDATADIR)
	install -m 755 -p kcpuid $(DESTDIR)$(BINDIR)/kcpuid
	install -m 444 -p cpuid.csv $(DESTDIR)$(HWDATADIR)/cpuid.csv
