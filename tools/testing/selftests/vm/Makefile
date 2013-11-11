# Makefile for vm selftests

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall

all: hugepage-mmap hugepage-shm  map_hugetlb thuge-gen
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

run_tests: all
	@/bin/sh ./run_vmtests || echo "vmtests: [FAIL]"

clean:
	$(RM) hugepage-mmap hugepage-shm  map_hugetlb
