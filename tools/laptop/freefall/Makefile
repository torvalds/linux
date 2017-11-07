# SPDX-License-Identifier: GPL-2.0
PREFIX ?= /usr
SBINDIR ?= sbin
INSTALL ?= install
CC = $(CROSS_COMPILE)gcc

TARGET = freefall

all: $(TARGET)

%: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	$(RM) $(TARGET)

install: freefall
	$(INSTALL) -D -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/$(SBINDIR)/$(TARGET)
