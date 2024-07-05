# SPDX-License-Identifier: GPL-2.0
uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

TEST_GEN_PROGS := vdso_test_gettimeofday
TEST_GEN_PROGS += vdso_test_getcpu
TEST_GEN_PROGS += vdso_test_abi
TEST_GEN_PROGS += vdso_test_clock_getres
ifeq ($(ARCH),$(filter $(ARCH),x86 x86_64))
TEST_GEN_PROGS += vdso_standalone_test_x86
endif
TEST_GEN_PROGS += vdso_test_correctness

CFLAGS := -std=gnu99

ifeq ($(CONFIG_X86_32),y)
LDLIBS += -lgcc_s
endif

include ../lib.mk
$(OUTPUT)/vdso_test_gettimeofday: parse_vdso.c vdso_test_gettimeofday.c
$(OUTPUT)/vdso_test_getcpu: parse_vdso.c vdso_test_getcpu.c
$(OUTPUT)/vdso_test_abi: parse_vdso.c vdso_test_abi.c
$(OUTPUT)/vdso_test_clock_getres: vdso_test_clock_getres.c

$(OUTPUT)/vdso_standalone_test_x86: vdso_standalone_test_x86.c parse_vdso.c
$(OUTPUT)/vdso_standalone_test_x86: CFLAGS +=-nostdlib -fno-asynchronous-unwind-tables -fno-stack-protector

$(OUTPUT)/vdso_test_correctness: vdso_test_correctness.c
$(OUTPUT)/vdso_test_correctness: LDFLAGS += -ldl
