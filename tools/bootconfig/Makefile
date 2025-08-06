# SPDX-License-Identifier: GPL-2.0
# Makefile for bootconfig command
include ../scripts/Makefile.include

bindir ?= /usr/bin

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

LIBSRC = $(srctree)/lib/bootconfig.c $(srctree)/include/linux/bootconfig.h
override CFLAGS += -Wall -g -I$(CURDIR)/include

ALL_TARGETS := bootconfig
ALL_PROGRAMS := $(patsubst %,$(OUTPUT)%,$(ALL_TARGETS))

all: $(ALL_PROGRAMS) test

$(OUTPUT)bootconfig: main.c include/linux/bootconfig.h $(LIBSRC)
	$(CC) $(filter %.c,$^) $(CFLAGS) $(LDFLAGS) -o $@

test: $(ALL_PROGRAMS) test-bootconfig.sh
	./test-bootconfig.sh $(OUTPUT)

install: $(ALL_PROGRAMS)
	install $(OUTPUT)bootconfig $(DESTDIR)$(bindir)

clean:
	$(RM) -f $(OUTPUT)*.o $(ALL_PROGRAMS)
