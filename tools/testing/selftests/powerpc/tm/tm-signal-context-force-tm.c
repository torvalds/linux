// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018, Breno Leitao, Gustavo Romero, IBM Corp.
 *
 * This test raises a SIGUSR1 signal, and toggle the MSR[TS]
 * fields at the signal handler. With MSR[TS] being set, the kernel will
 * force a recheckpoint, which may cause a segfault when returning to
 * user space. Since the test needs to re-run, the segfault needs to be
 * caught and handled.
 *
 * In order to continue the test even after a segfault, the context is
 * saved prior to the signal being raised, and it is restored when there is
 * a segmentation fault. This happens for COUNT_MAX times.
 *
 * This test never fails (as returning EXIT_FAILURE). It either succeeds,
 * or crash the kernel (on a buggy kernel).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/mman.h>

#include "tm.h"
#include "utils.h"
#include "reg.h"

#define COUNT_MAX       5000		/* Number of interactions */

/*
 * This test only runs on 64 bits system. Unsetting MSR_TS_S to avoid
 * compilation issue on 32 bits system. There is no side effect, since the
 * whole test will be skipped if it is not running on 64 bits system.
 */
#ifndef __powerpc64__
#undef  MSR_TS_S
#define MSR_TS_S	0
#endif

/* Setting contexts because the test will crash and we want to recover */
ucontext_t init_context;

/* count is changed in the signal handler, so it must be volatile */
static volatile int count;

void usr_signal_handler(int signo, siginfo_t *si, void *uc)
{
	ucontext_t *ucp = uc;
	int ret;

	/*
	 * Allocating memory in a signal handler, and never freeing it on
	 * purpose, forcing the heap increase, so, the memory leak is what
	 * we want here.
	 */
	ucp->uc_link = mmap(NULL, sizeof(ucontext_t),
			    PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	if (ucp->uc_link == (void *)-1) {
		perror("Mmap failed");
		exit(-1);
	}

	/* Forcing the page to be allocated in a page fault */
	ret = madvise(ucp->uc_link, sizeof(ucontext_t), MADV_DONTNEED);
	if (ret) {
		perror("madvise failed");
		exit(-1);
	}

	memcpy(&ucp->uc_link->uc_mcontext, &ucp->uc_mcontext,
		sizeof(ucp->uc_mcontext));

	/* Forcing to enable MSR[TM] */
	UCONTEXT_MSR(ucp) |= MSR_TS_S;

	/*
	 * A fork inside a signal handler seems to be more efficient than a
	 * fork() prior to the signal being raised.
	 */
	if (fork() == 0) {
		/*
		 * Both child and parent will return, but, child returns
		 * with count set so it will exit in the next segfault.
		 * Parent will continue to loop.
		 */
		count = COUNT_MAX;
	}

	/*
	 * If the change above does not hit the bug, it will cause a
	 * segmentation fault, since the ck structures are NULL.
	 */
}

void seg_signal_handler(int signo, siginfo_t *si, void *uc)
{
	count++;

	/* Reexecute the test */
	setcontext(&init_context);
}

void tm_trap_test(void)
{
	struct sigaction usr_sa, seg_sa;
	stack_t ss;

	usr_sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	usr_sa.sa_sigaction = usr_signal_handler;

	seg_sa.sa_flags = SA_SIGINFO;
	seg_sa.sa_sigaction = seg_signal_handler;

	/*
	 * Set initial context. Will get back here from
	 * seg_signal_handler()
	 */
	getcontext(&init_context);

	while (count < COUNT_MAX) {
		/* Allocated an alternative signal stack area */
		ss.ss_sp = mmap(NULL, SIGSTKSZ, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
		ss.ss_size = SIGSTKSZ;
		ss.ss_flags = 0;

		if (ss.ss_sp == (void *)-1) {
			perror("mmap error\n");
			exit(-1);
		}

		/* Force the allocation through a page fault */
		if (madvise(ss.ss_sp, SIGSTKSZ, MADV_DONTNEED)) {
			perror("madvise\n");
			exit(-1);
		}

		/*
		 * Setting an alternative stack to generate a page fault when
		 * the signal is raised.
		 */
		if (sigaltstack(&ss, NULL)) {
			perror("sigaltstack\n");
			exit(-1);
		}

		/* The signal handler will enable MSR_TS */
		sigaction(SIGUSR1, &usr_sa, NULL);
		/* If it does not crash, it might segfault, avoid it to retest */
		sigaction(SIGSEGV, &seg_sa, NULL);

		raise(SIGUSR1);
		count++;
	}
}

int tm_signal_context_force_tm(void)
{
	SKIP_IF(!have_htm());
	/*
	 * Skipping if not running on 64 bits system, since I think it is
	 * not possible to set mcontext's [MSR] with TS, due to it being 32
	 * bits.
	 */
	SKIP_IF(!is_ppc64le());

	tm_trap_test();

	return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
	return test_harness(tm_signal_context_force_tm, "tm_signal_context_force_tm");
}
