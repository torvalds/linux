# SPDX-License-Identifier: GPL-2.0
all:

include ../lib.mk

.PHONY: all clean

BINARIES := uevent_filtering
CFLAGS += -Wl,-no-as-needed -Wall

uevent_filtering: uevent_filtering.c ../kselftest.h ../kselftest_harness.h
	$(CC) $(CFLAGS) $< -o $@

TEST_PROGS += $(BINARIES)
EXTRA_CLEAN := $(BINARIES)

all: $(BINARIES)
