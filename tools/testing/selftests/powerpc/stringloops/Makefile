# SPDX-License-Identifier: GPL-2.0
EXTRA_SOURCES := ../harness.c

build_32bit = $(shell if ($(CC) $(CFLAGS) -m32 -o /dev/null memcmp.c >/dev/null 2>&1) then echo "1"; fi)

TEST_GEN_PROGS := memcmp_64 strlen

$(OUTPUT)/memcmp_64: memcmp.c ../utils.c
$(OUTPUT)/memcmp_64: CFLAGS += -m64 -maltivec

ifeq ($(build_32bit),1)
$(OUTPUT)/memcmp_32: memcmp.c
$(OUTPUT)/memcmp_32: CFLAGS += -m32

TEST_GEN_PROGS += memcmp_32
endif

$(OUTPUT)/strlen: strlen.c string.c

ifeq ($(build_32bit),1)
$(OUTPUT)/strlen_32: strlen.c
$(OUTPUT)/strlen_32: CFLAGS += -m32

TEST_GEN_PROGS += strlen_32
endif

top_srcdir = ../../../../..
include ../../lib.mk
include ../flags.mk

# The loops are all 64-bit code
CFLAGS += -I$(CURDIR)

ASFLAGS = $(CFLAGS)

$(TEST_GEN_PROGS): $(EXTRA_SOURCES)
