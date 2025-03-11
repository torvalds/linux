/*
 * Copyright IBM Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include "utils.h"

char *file_name;

int in_test;
volatile int faulted;
volatile void *dar;
int errors;

static void segv(int signum, siginfo_t *info, void *ctxt_v)
{
	ucontext_t *ctxt = (ucontext_t *)ctxt_v;
	struct pt_regs *regs = ctxt->uc_mcontext.regs;

	if (!in_test) {
		fprintf(stderr, "Segfault outside of test !\n");
		exit(1);
	}

	faulted = 1;
	dar = (void *)regs->dar;
	regs->nip += 4;
}

static inline void do_read(const volatile void *addr)
{
	int ret;

	asm volatile("lwz %0,0(%1); twi 0,%0,0; isync;\n"
		     : "=r" (ret) : "r" (addr) : "memory");
}

static inline void do_write(const volatile void *addr)
{
	int val = 0x1234567;

	asm volatile("stw %0,0(%1); sync; \n"
		     : : "r" (val), "r" (addr) : "memory");
}

static inline void check_faulted(void *addr, long page, long subpage, int write)
{
	int want_fault = (subpage == ((page + 3) % 16));

	if (write)
		want_fault |= (subpage == ((page + 1) % 16));

	if (faulted != want_fault) {
		printf("Failed at %p (p=%ld,sp=%ld,w=%d), want=%s, got=%s !\n",
		       addr, page, subpage, write,
		       want_fault ? "fault" : "pass",
		       faulted ? "fault" : "pass");
		++errors;
	}

	if (faulted) {
		if (dar != addr) {
			printf("Fault expected at %p and happened at %p !\n",
			       addr, dar);
		}
		faulted = 0;
		asm volatile("sync" : : : "memory");
	}
}

static int run_test(void *addr, unsigned long size)
{
	unsigned int *map;
	long i, j, pages, err;

	pages = size / 0x10000;
	map = malloc(pages * 4);
	assert(map);

	/*
	 * for each page, mark subpage i % 16 read only and subpage
	 * (i + 3) % 16 inaccessible
	 */
	for (i = 0; i < pages; i++) {
		map[i] = (0x40000000 >> (((i + 1) * 2) % 32)) |
			(0xc0000000 >> (((i + 3) * 2) % 32));
	}

	err = syscall(__NR_subpage_prot, addr, size, map);
	if (err) {
		perror("subpage_perm");
		return 1;
	}
	free(map);

	in_test = 1;
	errors = 0;
	for (i = 0; i < pages; i++) {
		for (j = 0; j < 16; j++, addr += 0x1000) {
			do_read(addr);
			check_faulted(addr, i, j, 0);
			do_write(addr);
			check_faulted(addr, i, j, 1);
		}
	}

	in_test = 0;
	if (errors) {
		printf("%d errors detected\n", errors);
		return 1;
	}

	return 0;
}

static int syscall_available(void)
{
	int rc;

	errno = 0;
	rc = syscall(__NR_subpage_prot, 0, 0, 0);

	return rc == 0 || (errno != ENOENT && errno != ENOSYS);
}

int test_anon(void)
{
	unsigned long align;
	struct sigaction act = {
		.sa_sigaction = segv,
		.sa_flags = SA_SIGINFO
	};
	void *mallocblock;
	unsigned long mallocsize;

	SKIP_IF(!syscall_available());

	if (getpagesize() != 0x10000) {
		fprintf(stderr, "Kernel page size must be 64K!\n");
		return 1;
	}

	sigaction(SIGSEGV, &act, NULL);

	mallocsize = 4 * 16 * 1024 * 1024;

	FAIL_IF(posix_memalign(&mallocblock, 64 * 1024, mallocsize));

	align = (unsigned long)mallocblock;
	if (align & 0xffff)
		align = (align | 0xffff) + 1;

	mallocblock = (void *)align;

	printf("allocated malloc block of 0x%lx bytes at %p\n",
	       mallocsize, mallocblock);

	printf("testing malloc block...\n");

	return run_test(mallocblock, mallocsize);
}

int test_file(void)
{
	struct sigaction act = {
		.sa_sigaction = segv,
		.sa_flags = SA_SIGINFO
	};
	void *fileblock;
	off_t filesize;
	int fd;

	SKIP_IF(!syscall_available());

	fd = open(file_name, O_RDWR);
	if (fd == -1) {
		perror("failed to open file");
		return 1;
	}
	sigaction(SIGSEGV, &act, NULL);

	filesize = lseek(fd, 0, SEEK_END);
	if (filesize & 0xffff)
		filesize &= ~0xfffful;

	fileblock = mmap(NULL, filesize, PROT_READ | PROT_WRITE,
			 MAP_SHARED, fd, 0);
	if (fileblock == MAP_FAILED) {
		perror("failed to map file");
		return 1;
	}
	printf("allocated %s for 0x%llx bytes at %p\n",
	       file_name, (long long)filesize, fileblock);

	printf("testing file map...\n");

	return run_test(fileblock, filesize);
}

int main(int argc, char *argv[])
{
	int rc;

	rc = test_harness(test_anon, "subpage_prot_anon");
	if (rc)
		return rc;

	if (argc > 1)
		file_name = argv[1];
	else
		file_name = "tempfile";

	return test_harness(test_file, "subpage_prot_file");
}
