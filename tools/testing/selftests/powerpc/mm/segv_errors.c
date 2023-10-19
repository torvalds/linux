// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2017 John Sperbeck
 *
 * Test that an access to a mapped but inaccessible area causes a SEGV and
 * reports si_code == SEGV_ACCERR.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <assert.h>
#include <ucontext.h>

#include "utils.h"

static bool faulted;
static int si_code;

static void segv_handler(int n, siginfo_t *info, void *ctxt_v)
{
	ucontext_t *ctxt = (ucontext_t *)ctxt_v;
	struct pt_regs *regs = ctxt->uc_mcontext.regs;

	faulted = true;
	si_code = info->si_code;
	regs->nip += 4;
}

int test_segv_errors(void)
{
	struct sigaction act = {
		.sa_sigaction = segv_handler,
		.sa_flags = SA_SIGINFO,
	};
	char c, *p = NULL;

	p = mmap(NULL, getpagesize(), 0, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	FAIL_IF(p == MAP_FAILED);

	FAIL_IF(sigaction(SIGSEGV, &act, NULL) != 0);

	faulted = false;
	si_code = 0;

	/*
	 * We just need a compiler barrier, but mb() works and has the nice
	 * property of being easy to spot in the disassembly.
	 */
	mb();
	c = *p;
	mb();

	FAIL_IF(!faulted);
	FAIL_IF(si_code != SEGV_ACCERR);

	faulted = false;
	si_code = 0;

	mb();
	*p = c;
	mb();

	FAIL_IF(!faulted);
	FAIL_IF(si_code != SEGV_ACCERR);

	return 0;
}

int main(void)
{
	return test_harness(test_segv_errors, "segv_errors");
}
