# SPDX-License-Identifier: GPL-2.0

TM_TESTS := ptrace-tm-gpr
TM_TESTS += ptrace-tm-spd-gpr
TM_TESTS += ptrace-tm-spd-tar
TM_TESTS += ptrace-tm-spd-vsx
TM_TESTS += ptrace-tm-spr
TM_TESTS += ptrace-tm-tar
TM_TESTS += ptrace-tm-vsx

TESTS_64 := $(TM_TESTS)
TESTS_64 += core-pkey
TESTS_64 += perf-hwbreak
TESTS_64 += ptrace-hwbreak
TESTS_64 += ptrace-perf-hwbreak
TESTS_64 += ptrace-pkey
TESTS_64 += ptrace-syscall
TESTS_64 += ptrace-tar
TESTS_64 += ptrace-vsx

TESTS += ptrace-gpr

TEST_GEN_PROGS := $(TESTS) $(TESTS_64)

LOCAL_HDRS += $(patsubst %,$(selfdir)/powerpc/ptrace/%,$(wildcard *.h))

top_srcdir = ../../../../..
include ../../lib.mk

TM_TESTS := $(patsubst %,$(OUTPUT)/%,$(TM_TESTS))
TESTS_64 := $(patsubst %,$(OUTPUT)/%,$(TESTS_64))

$(TESTS_64): CFLAGS += -m64
$(TM_TESTS): CFLAGS += -I../tm -mhtm

CFLAGS += $(KHDR_INCLUDES) -fno-pie

$(OUTPUT)/ptrace-gpr: ptrace-gpr.S
$(OUTPUT)/ptrace-perf-hwbreak: ptrace-perf-asm.S
$(OUTPUT)/ptrace-pkey $(OUTPUT)/core-pkey: LDLIBS += -pthread

$(TEST_GEN_PROGS): ../harness.c ../utils.c ../lib/reg.S
