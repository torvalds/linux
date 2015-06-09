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
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

#define FIXUP_SECTION ".ex_fixup"

#include "word-at-a-time.h"

#include "utils.h"


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

#if defined(__powerpc64__)
#define UCONTEXT_NIA(UC)	(UC)->uc_mcontext.gp_regs[PT_NIP]
#elif defined(__powerpc__)
#define UCONTEXT_NIA(UC)	(UC)->uc_mcontext.uc_regs->gregs[PT_NIP]
#else
#error implement UCONTEXT_NIA
#endif

static int segv_error;

static void segv_handler(int signr, siginfo_t *info, void *ptr)
{
	ucontext_t *uc = (ucontext_t *)ptr;
	unsigned long addr = (unsigned long)info->si_addr;
	unsigned long *ip = &UCONTEXT_NIA(uc);
	unsigned long *ex_p = (unsigned long *)__start___ex_table;

	while (ex_p < (unsigned long *)__stop___ex_table) {
		unsigned long insn, fixup;

		insn = *ex_p++;
		fixup = *ex_p++;

		if (insn == *ip) {
			*ip = fixup;
			return;
		}
	}

	printf("No exception table match for NIA %lx ADDR %lx\n", *ip, addr);
	segv_error++;
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

	if (should != got)
		printf("offset %u load_unaligned_zeropad returned 0x%lx, should be 0x%lx\n", page_offset, got, should);

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

	FAIL_IF(segv_error);

	return 0;
}

int main(void)
{
	return test_harness(test_body, "load_unaligned_zeropad");
}
