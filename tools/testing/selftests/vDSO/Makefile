# SPDX-License-Identifier: GPL-2.0
include ../lib.mk

uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/x86/ -e s/x86_64/x86/)

TEST_GEN_PROGS := $(OUTPUT)/vdso_test_gettimeofday $(OUTPUT)/vdso_test_getcpu
ifeq ($(ARCH),x86)
TEST_GEN_PROGS += $(OUTPUT)/vdso_standalone_test_x86
endif

ifndef CROSS_COMPILE
CFLAGS := -std=gnu99
CFLAGS_vdso_standalone_test_x86 := -nostdlib -fno-asynchronous-unwind-tables -fno-stack-protector
ifeq ($(CONFIG_X86_32),y)
LDLIBS += -lgcc_s
endif

all: $(TEST_GEN_PROGS)
$(OUTPUT)/vdso_test_gettimeofday: parse_vdso.c vdso_test_gettimeofday.c
$(OUTPUT)/vdso_test_getcpu: parse_vdso.c vdso_test_getcpu.c
$(OUTPUT)/vdso_standalone_test_x86: vdso_standalone_test_x86.c parse_vdso.c
	$(CC) $(CFLAGS) $(CFLAGS_vdso_standalone_test_x86) \
		vdso_standalone_test_x86.c parse_vdso.c \
		-o $@

endif
