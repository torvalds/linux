# SPDX-License-Identifier: GPL-2.0
include ../../../scripts/Makefile.arch

TEST_GEN_PROGS := vdso_test_gettimeofday
TEST_GEN_PROGS += vdso_test_getcpu
TEST_GEN_PROGS += vdso_test_abi
ifeq ($(ARCH),$(filter $(ARCH),x86 x86_64))
TEST_GEN_PROGS += vdso_standalone_test_x86
endif
TEST_GEN_PROGS += vdso_test_correctness
TEST_GEN_PROGS += vdso_test_getrandom
TEST_GEN_PROGS += vdso_test_chacha

CFLAGS := -std=gnu99 -O2 -Wall -Wstrict-prototypes

ifeq ($(CONFIG_X86_32),y)
LDLIBS += -lgcc_s
endif

include ../lib.mk

CFLAGS += $(TOOLS_INCLUDES)

CFLAGS_NOLIBC := -nostdlib -nostdinc -ffreestanding -fno-asynchronous-unwind-tables \
		 -fno-stack-protector -include $(top_srcdir)/tools/include/nolibc/nolibc.h \
		 -I$(top_srcdir)/tools/include/nolibc/ $(KHDR_INCLUDES)

$(OUTPUT)/vdso_test_gettimeofday: parse_vdso.c vdso_test_gettimeofday.c
$(OUTPUT)/vdso_test_getcpu: parse_vdso.c vdso_test_getcpu.c
$(OUTPUT)/vdso_test_abi: parse_vdso.c vdso_test_abi.c

$(OUTPUT)/vdso_standalone_test_x86: vdso_standalone_test_x86.c parse_vdso.c | headers
$(OUTPUT)/vdso_standalone_test_x86: CFLAGS:=$(CFLAGS_NOLIBC) $(CFLAGS)

$(OUTPUT)/vdso_test_correctness: vdso_test_correctness.c
$(OUTPUT)/vdso_test_correctness: LDFLAGS += -ldl

$(OUTPUT)/vdso_test_getrandom: parse_vdso.c
$(OUTPUT)/vdso_test_getrandom: CFLAGS += -isystem $(top_srcdir)/tools/include \
                                         $(KHDR_INCLUDES) \
                                         -isystem $(top_srcdir)/include/uapi

$(OUTPUT)/vdso_test_chacha: vgetrandom-chacha.S
$(OUTPUT)/vdso_test_chacha: CFLAGS += -idirafter $(top_srcdir)/tools/include \
                                      -idirafter $(top_srcdir)/tools/include/generated \
                                      -idirafter $(top_srcdir)/arch/$(SRCARCH)/include \
                                      -idirafter $(top_srcdir)/include \
                                      -Wa,--noexecstack
