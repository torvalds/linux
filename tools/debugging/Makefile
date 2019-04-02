# SPDX-License-Identifier: GPL-2.0
# Makefile for deging tools

PREFIX ?= /usr
BINDIR ?= bin
INSTALL ?= install

TARGET = kernel-chktaint

all: $(TARGET)

clean:

install: kernel-chktaint
	$(INSTALL) -D -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/$(BINDIR)/$(TARGET)

