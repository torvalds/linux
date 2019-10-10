// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Userspace test harness for load_unaligned_zeropad. Creates two
 * pages and uses mprotect to prevent access to the second page and
 * a SEGV handler that walks the exception tables and runs the fixup
 * routine.
 *
 * The results are compared against a normal load that is that is
 * performed while access to the second page is enabled via mprotect.
 *
 * Copyright (C) 2014 Anton Blanchard <anton@au.ibm.com>, IBM
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

#define FIXUP_SECTION ".ex_fixup"

static inline unsigned long __fls(unsigned long x);

#include "word-at-a-time.h"

#include "utils.h"

static inline unsigned long __fls(unsigned long x)
{
	int lz;

	asm (PPC_CNTLZL "%0,%1" : "=r" (lz) : "r" (x));
	return sizeof(unsigned long) - 1 - lz;
}

static int page_size;
static char *mem_region;

static int protect_region(void)
{
	if (mprotect(mem_region + page_size, page_size, PROT_NONE)) {
		perror("mprotect");
		return 1;
	}

	return 0;
}

static int unprotect_region(void)
{
	if (mprotect(mem_region + page_size, page_size, PROT_READ|PROT_WRITE)) {
		perror("mprotect");
		return 1;
	}

	return 0;
}

extern char __start___ex_table[];
extern char __stop___ex_table[];

struct extbl_entry {
	int insn;
	int fixup;
};

static void segv_handler(int signr, siginfo_t *info, void *ptr)
{
	ucontext_t *uc = (ucontext_t *)ptr;
	unsigned long addr = (unsigned long)info->si_addr;
	unsigned long *ip = &UCONTEXT_NIA(uc);
	struct extbl_entry *entry = (struct extbl_entry *)__start___ex_table;

	while (entry < (struct extbl_entry *)__stop___ex_table) {
		unsigned long insn, fixup;

		insn  = (unsigned long)&entry->insn + entry->insn;
		fixup = (unsigned long)&entry->fixup + entry->fixup;

		if (insn == *ip) {
			*ip = fixup;
			return;
		}
	}

	printf("No exception table match for NIA %lx ADDR %lx\n", *ip, addr);
	abort();
}

static void setup_segv_handler(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_sigaction = segv_handler;
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &action, NULL);
}

static int do_one_test(char *p, int page_offset)
{
	unsigned long should;
	unsigned long got;

	FAIL_IF(unprotect_region());
	should = *(unsigned long *)p;
	FAIL_IF(protect_region());

	got = load_unaligned_zeropad(p);

	if (should != got) {
		printf("offset %u load_unaligned_zeropad returned 0x%lx, should be 0x%lx\n", page_offset, got, should);
		return 1;
	}

	return 0;
}

static int test_body(void)
{
	unsigned long i;

	page_size = getpagesize();
	mem_region = mmap(NULL, page_size * 2, PROT_READ|PROT_WRITE,
		MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

	FAIL_IF(mem_region == MAP_FAILED);

	for (i = 0; i < page_size; i++)
		mem_region[i] = i;

	memset(mem_region+page_size, 0, page_size);

	setup_segv_handler();

	for (i = 0; i < page_size; i++)
		FAIL_IF(do_one_test(mem_region+i, i));

	return 0;
}

int main(void)
{
	return test_harness(test_body, "load_unaligned_zeropad");
}
