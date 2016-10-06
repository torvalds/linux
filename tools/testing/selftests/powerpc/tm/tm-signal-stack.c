/*
 * Copyright 2015, Michael Neuling, IBM Corp.
 * Licensed under GPLv2.
 *
 * Test the kernel's signal delievery code to ensure that we don't
 * trelaim twice in the kernel signal delivery code.  This can happen
 * if we trigger a signal when in a transaction and the stack pointer
 * is bogus.
 *
 * This test case registers a SEGV handler, sets the stack pointer
 * (r1) to NULL, starts a transaction and then generates a SEGV.  The
 * SEGV should be handled but we exit here as the stack pointer is
 * invalid and hance we can't sigreturn.  We only need to check that
 * this flow doesn't crash the kernel.
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include "utils.h"
#include "tm.h"

void signal_segv(int signum)
{
	/* This should never actually run since stack is foobar */
	exit(1);
}

int tm_signal_stack()
{
	int pid;

	SKIP_IF(!have_htm());

	pid = fork();
	if (pid < 0)
		exit(1);

	if (pid) { /* Parent */
		/*
		 * It's likely the whole machine will crash here so if
		 * the child ever exits, we are good.
		 */
		wait(NULL);
		return 0;
	}

	/*
	 * The flow here is:
	 * 1) register a signal handler (so signal delievery occurs)
	 * 2) make stack pointer (r1) = NULL
	 * 3) start transaction
	 * 4) cause segv
	 */
	if (signal(SIGSEGV, signal_segv) == SIG_ERR)
		exit(1);
	asm volatile("li 1, 0 ;"		/* stack ptr == NULL */
		     "1:"
		     "tbegin.;"
		     "beq 1b ;"			/* retry forever */
		     "tsuspend.;"
		     "ld 2, 0(1) ;"		/* trigger segv" */
		     : : : "memory");

	/* This should never get here due to above segv */
	return 1;
}

int main(void)
{
	return test_harness(tm_signal_stack, "tm_signal_stack");
}
