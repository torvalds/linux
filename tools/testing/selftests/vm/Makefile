# Makefile for vm selftests

CFLAGS = -Wall -I ../../../../usr/include $(EXTRA_CFLAGS)
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

%: %.c
	$(CC) $(CFLAGS) -o $@ $^ -lrt
userfaultfd: userfaultfd.c ../../../../usr/include/linux/kernel.h
	$(CC) $(CFLAGS) -O2 -o $@ $< -lpthread

mlock-random-test: mlock-random-test.c
	$(CC) $(CFLAGS) -o $@ $< -lcap

../../../../usr/include/linux/kernel.h:
	make -C ../../../.. headers_install

TEST_PROGS := run_vmtests

include ../lib.mk

