# SPDX-License-Identifier: GPL-2.0
# Makefile for Intel Software Defined Silicon provisioning tool

dell-uart-backlight-emulator: dell-uart-backlight-emulator.c

BINDIR ?= /usr/bin

override CFLAGS += -O2 -Wall

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

.PHONY : clean
clean :
	@rm -f dell-uart-backlight-emulator

install : dell-uart-backlight-emulator
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 -p dell-uart-backlight-emulator $(DESTDIR)$(BINDIR)/dell-uart-backlight-emulator
