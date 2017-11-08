# SPDX-License-Identifier: GPL-2.0
# Makefile for vm selftests

ifndef OUTPUT
  OUTPUT := $(shell pwd)
endif

CFLAGS = -Wall -I ../../../../usr/include $(EXTRA_CFLAGS)
LDLIBS = -lrt
TEST_GEN_FILES = compaction_test
TEST_GEN_FILES += hugepage-mmap
TEST_GEN_FILES += hugepage-shm
TEST_GEN_FILES += map_hugetlb
TEST_GEN_FILES += mlock2-tests
TEST_GEN_FILES += on-fault-limit
TEST_GEN_FILES += thuge-gen
TEST_GEN_FILES += transhuge-stress
TEST_GEN_FILES += userfaultfd
TEST_GEN_FILES += mlock-random-test
TEST_GEN_FILES += virtual_address_range

TEST_PROGS := run_vmtests

include ../lib.mk

$(OUTPUT)/userfaultfd: ../../../../usr/include/linux/kernel.h
$(OUTPUT)/userfaultfd: LDLIBS += -lpthread

$(OUTPUT)/mlock-random-test: LDLIBS += -lcap

../../../../usr/include/linux/kernel.h:
	make -C ../../../.. headers_install
