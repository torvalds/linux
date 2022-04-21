# SPDX-License-Identifier: GPL-2.0
# Makefile for Intel Software Defined Silicon provisioning tool

intel_sdsi: intel_sdsi.c

CFLAGS = -Wextra

BINDIR ?= /usr/sbin

override CFLAGS += -O2 -Wall

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

.PHONY : clean
clean :
	@rm -f intel_sdsi

install : intel_sdsi
	install -d  $(DESTDIR)$(BINDIR)
	install -m 755 -p intel_sdsi $(DESTDIR)$(BINDIR)/intel_sdsi
