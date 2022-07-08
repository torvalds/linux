# SPDX-License-Identifier: GPL-2.0
CFLAGS += -Wl,-no-as-needed -Wall -isystem ../../../../usr/include/
LDFLAGS += -lpthread
LDLIBS += -lcap

TEST_GEN_PROGS := seccomp_bpf seccomp_benchmark
include ../lib.mk
