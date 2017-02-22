# Makefile for vm selftests

CFLAGS = -Wall -I ../../../../usr/include $(EXTRA_CFLAGS)
BINARIES = compaction_test
BINARIES += hugepage-mmap
BINARIES += hugepage-shm
BINARIES += map_hugetlb
BINARIES += mlock2-tests
BINARIES += on-fault-limit
BINARIES += thuge-gen
BINARIES += transhuge-stress
BINARIES += userfaultfd
BINARIES += userfaultfd_hugetlb
BINARIES += userfaultfd_shmem
BINARIES += mlock-random-test

all: $(BINARIES)
%: %.c
	$(CC) $(CFLAGS) -o $@ $^ -lrt
userfaultfd: userfaultfd.c ../../../../usr/include/linux/kernel.h
	$(CC) $(CFLAGS) -O2 -o $@ $< -lpthread

userfaultfd_hugetlb: userfaultfd.c ../../../../usr/include/linux/kernel.h
	$(CC) $(CFLAGS) -DHUGETLB_TEST -O2 -o $@ $< -lpthread

userfaultfd_shmem: userfaultfd.c ../../../../usr/include/linux/kernel.h
	$(CC) $(CFLAGS) -DSHMEM_TEST -O2 -o $@ $< -lpthread

mlock-random-test: mlock-random-test.c
	$(CC) $(CFLAGS) -o $@ $< -lcap

../../../../usr/include/linux/kernel.h:
	make -C ../../../.. headers_install

TEST_PROGS := run_vmtests
TEST_FILES := $(BINARIES)

include ../lib.mk

clean:
	$(RM) $(BINARIES)
