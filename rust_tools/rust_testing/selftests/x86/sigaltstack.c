// SPDX-License-Identifier: GPL-2.0-only

#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <setjmp.h>

#include "helpers.h"

/* sigaltstack()-enforced minimum stack */
#define ENFORCED_MINSIGSTKSZ	2048

#ifndef AT_MINSIGSTKSZ
#  define AT_MINSIGSTKSZ	51
#endif

static int nerrs;

static bool sigalrm_expected;

static unsigned long at_minstack_size;

static int setup_altstack(void *start, unsigned long size)
{
	stack_t ss;

	memset(&ss, 0, sizeof(ss));
	ss.ss_size = size;
	ss.ss_sp = start;

	return sigaltstack(&ss, NULL);
}

static jmp_buf jmpbuf;

static void sigsegv(int sig, siginfo_t *info, void *ctx_void)
{
	if (sigalrm_expected) {
		printf("[FAIL]\tWrong signal delivered: SIGSEGV (expected SIGALRM).");
		nerrs++;
	} else {
		printf("[OK]\tSIGSEGV signal delivered.\n");
	}

	siglongjmp(jmpbuf, 1);
}

static void sigalrm(int sig, siginfo_t *info, void *ctx_void)
{
	if (!sigalrm_expected) {
		printf("[FAIL]\tWrong signal delivered: SIGALRM (expected SIGSEGV).");
		nerrs++;
	} else {
		printf("[OK]\tSIGALRM signal delivered.\n");
	}
}

static void test_sigaltstack(void *altstack, unsigned long size)
{
	if (setup_altstack(altstack, size))
		err(1, "sigaltstack()");

	sigalrm_expected = (size > at_minstack_size) ? true : false;

	sethandler(SIGSEGV, sigsegv, 0);
	sethandler(SIGALRM, sigalrm, SA_ONSTACK);

	if (!sigsetjmp(jmpbuf, 1)) {
		printf("[RUN]\tTest an alternate signal stack of %ssufficient size.\n",
		       sigalrm_expected ? "" : "in");
		printf("\tRaise SIGALRM. %s is expected to be delivered.\n",
		       sigalrm_expected ? "It" : "SIGSEGV");
		raise(SIGALRM);
	}

	clearhandler(SIGALRM);
	clearhandler(SIGSEGV);
}

int main(void)
{
	void *altstack;

	at_minstack_size = getauxval(AT_MINSIGSTKSZ);

	altstack = mmap(NULL, at_minstack_size + SIGSTKSZ, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	if (altstack == MAP_FAILED)
		err(1, "mmap()");

	if ((ENFORCED_MINSIGSTKSZ + 1) < at_minstack_size)
		test_sigaltstack(altstack, ENFORCED_MINSIGSTKSZ + 1);

	test_sigaltstack(altstack, at_minstack_size + SIGSTKSZ);

	return nerrs == 0 ? 0 : 1;
}
