# SPDX-License-Identifier: GPL-2.0
all:

include ../lib.mk

.PHONY: all clean

BINARIES := seccomp_bpf seccomp_benchmark
CFLAGS += -Wl,-no-as-needed -Wall

seccomp_bpf: seccomp_bpf.c ../kselftest_harness.h
	$(CC) $(CFLAGS) $(LDFLAGS) $< -lpthread -o $@

TEST_PROGS += $(BINARIES)
EXTRA_CLEAN := $(BINARIES)

all: $(BINARIES)
