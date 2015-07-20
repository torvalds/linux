# Makefile for vm selftests

CFLAGS = -Wall
BINARIES = compaction_test
BINARIES += hugepage-mmap
BINARIES += hugepage-shm
BINARIES += hugetlbfstest
BINARIES += map_hugetlb
BINARIES += thuge-gen
BINARIES += transhuge-stress

all: $(BINARIES)
%: %.c
	$(CC) $(CFLAGS) -o $@ $^ -lrt

TEST_PROGS := run_vmtests
TEST_FILES := $(BINARIES)

include ../lib.mk

clean:
	$(RM) $(BINARIES)
