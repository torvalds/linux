# Makefile for vm selftests

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra

all: hugepage-mmap hugepage-shm  map_hugetlb
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

run_tests: all
	/bin/sh ./run_vmtests

clean:
	$(RM) hugepage-mmap hugepage-shm  map_hugetlb
