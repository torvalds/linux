# Makefile for vm selftests

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall
BINARIES = hugepage-mmap hugepage-shm map_hugetlb thuge-gen hugetlbfstest
BINARIES += transhuge-stress

all: $(BINARIES)
%: %.c
	$(CC) $(CFLAGS) -o $@ $^ -lrt

run_tests: all
	@/bin/sh ./run_vmtests || (echo "vmtests: [FAIL]"; exit 1)

clean:
	$(RM) $(BINARIES)
