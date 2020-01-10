# SPDX-License-Identifier: GPL-2.0
# Makefile for bootconfig command

bindir ?= /usr/bin

HEADER = include/linux/bootconfig.h
CFLAGS = -Wall -g -I./include

PROGS = bootconfig

all: $(PROGS)

bootconfig: ../../lib/bootconfig.c main.c $(HEADER)
	$(CC) $(filter %.c,$^) $(CFLAGS) -o $@

install: $(PROGS)
	install bootconfig $(DESTDIR)$(bindir)

clean:
	$(RM) -f *.o bootconfig
