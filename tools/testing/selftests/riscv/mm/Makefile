# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 ARM Limited
# Originally tools/testing/arm64/abi/Makefile

# Additional include paths needed by kselftest.h and local headers
CFLAGS += -D_GNU_SOURCE -std=gnu99 -I.

TEST_GEN_FILES := mmap_default mmap_bottomup

TEST_PROGS := run_mmap.sh

include ../../lib.mk

$(OUTPUT)/mm: mmap_default.c mmap_bottomup.c mmap_tests.h
	$(CC) -o$@ $(CFLAGS) $(LDFLAGS) $^
