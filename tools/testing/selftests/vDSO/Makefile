# SPDX-License-Identifier: GPL-2.0
include ../lib.mk

ifndef CROSS_COMPILE
CFLAGS := -std=gnu99
CFLAGS_vdso_standalone_test_x86 := -nostdlib -fno-asynchronous-unwind-tables -fno-stack-protector
ifeq ($(CONFIG_X86_32),y)
LDLIBS += -lgcc_s
endif

TEST_PROGS := $(OUTPUT)/vdso_test $(OUTPUT)/vdso_standalone_test_x86

all: $(TEST_PROGS)
$(OUTPUT)/vdso_test: parse_vdso.c vdso_test.c
$(OUTPUT)/vdso_standalone_test_x86: vdso_standalone_test_x86.c parse_vdso.c
	$(CC) $(CFLAGS) $(CFLAGS_vdso_standalone_test_x86) \
		vdso_standalone_test_x86.c parse_vdso.c \
		-o $@

EXTRA_CLEAN := $(TEST_PROGS)
endif
