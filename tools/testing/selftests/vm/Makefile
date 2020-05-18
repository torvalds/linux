# SPDX-License-Identifier: GPL-2.0
# Makefile for vm selftests
uname_M := $(shell uname -m 2>/dev/null || echo not)
MACHINE ?= $(shell echo $(uname_M) | sed -e 's/aarch64.*/arm64/')

CFLAGS = -Wall -I ../../../../usr/include $(EXTRA_CFLAGS)
LDLIBS = -lrt
TEST_GEN_FILES = compaction_test
TEST_GEN_FILES += gup_benchmark
TEST_GEN_FILES += hugepage-mmap
TEST_GEN_FILES += hugepage-shm
TEST_GEN_FILES += map_hugetlb
TEST_GEN_FILES += map_fixed_noreplace
TEST_GEN_FILES += map_populate
TEST_GEN_FILES += mlock-random-test
TEST_GEN_FILES += mlock2-tests
TEST_GEN_FILES += mremap_dontunmap
TEST_GEN_FILES += on-fault-limit
TEST_GEN_FILES += thuge-gen
TEST_GEN_FILES += transhuge-stress
TEST_GEN_FILES += userfaultfd

ifneq (,$(filter $(MACHINE),arm64 ia64 mips64 parisc64 ppc64 ppc64le riscv64 s390x sh64 sparc64 x86_64))
TEST_GEN_FILES += va_128TBswitch
TEST_GEN_FILES += virtual_address_range
TEST_GEN_FILES += write_to_hugetlbfs
endif

TEST_PROGS := run_vmtests

TEST_FILES := test_vmalloc.sh

KSFT_KHDR_INSTALL := 1
include ../lib.mk

$(OUTPUT)/userfaultfd: LDLIBS += -lpthread

$(OUTPUT)/mlock-random-test: LDLIBS += -lcap
