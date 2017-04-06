# Makefile for vm selftests

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
TEST_GEN_FILES += userfaultfd_hugetlb
TEST_GEN_FILES += userfaultfd_shmem
TEST_GEN_FILES += mlock-random-test

TEST_PROGS := run_vmtests

include ../lib.mk

$(OUTPUT)/userfaultfd: LDLIBS += -lpthread ../../../../usr/include/linux/kernel.h

$(OUTPUT)/userfaultfd_hugetlb: userfaultfd.c ../../../../usr/include/linux/kernel.h
	$(CC) $(CFLAGS) -DHUGETLB_TEST -O2 -o $@ $< -lpthread

$(OUTPUT)/userfaultfd_shmem: userfaultfd.c  ../../../../usr/include/linux/kernel.h
	$(CC) $(CFLAGS) -DSHMEM_TEST -O2 -o $@ $< -lpthread

$(OUTPUT)/mlock-random-test: LDLIBS += -lcap

../../../../usr/include/linux/kernel.h:
	make -C ../../../.. headers_install
