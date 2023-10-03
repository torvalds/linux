# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 ARM Limited
# Originally tools/testing/arm64/abi/Makefile

# Additional include paths needed by kselftest.h and local headers
CFLAGS += -D_GNU_SOURCE -std=gnu99 -I.

TEST_GEN_FILES := testcases/mmap_default testcases/mmap_bottomup

TEST_PROGS := testcases/run_mmap.sh

include ../../lib.mk

$(OUTPUT)/mm: testcases/mmap_default.c testcases/mmap_bottomup.c testcases/mmap_tests.h
	$(CC) -o$@ $(CFLAGS) $(LDFLAGS) $^
