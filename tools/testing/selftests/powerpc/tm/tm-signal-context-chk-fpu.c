/*
 * Copyright 2016, Cyril Bur, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *
 * Test the kernel's signal frame code.
 *
 * The kernel sets up two sets of ucontexts if the signal was to be
 * delivered while the thread was in a transaction.
 * Expected behaviour is that the checkpointed state is in the user
 * context passed to the signal handler. The speculated state can be
 * accessed with the uc_link pointer.
 *
 * The rationale for this is that if TM unaware code (which linked
 * against TM libs) installs a signal handler it will not know of the
 * speculative nature of the 'live' registers and may infer the wrong
 * thing.
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include <altivec.h>

#include "utils.h"
#include "tm.h"

#define MAX_ATTEMPT 500000

#define NV_FPU_REGS 18

long tm_signal_self_context_load(pid_t pid, long *gprs, double *fps, vector int *vms, vector int *vss);

/* Be sure there are 2x as many as there are NV FPU regs (2x18) */
static double fps[] = {
	 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
	-1,-2,-3,-4,-5,-6,-7,-8,-9,-10,-11,-12,-13,-14,-15,-16,-17,-18
};

static sig_atomic_t fail;

static void signal_usr1(int signum, siginfo_t *info, void *uc)
{
	int i;
	ucontext_t *ucp = uc;
	ucontext_t *tm_ucp = ucp->uc_link;

	for (i = 0; i < NV_FPU_REGS && !fail; i++) {
		fail = (ucp->uc_mcontext.fp_regs[i + 14] != fps[i]);
		fail |= (tm_ucp->uc_mcontext.fp_regs[i + 14] != fps[i + NV_FPU_REGS]);
		if (fail)
			printf("Failed on %d FP %g or %g\n", i, ucp->uc_mcontext.fp_regs[i + 14], tm_ucp->uc_mcontext.fp_regs[i + 14]);
	}
}

static int tm_signal_context_chk_fpu()
{
	struct sigaction act;
	int i;
	long rc;
	pid_t pid = getpid();

	SKIP_IF(!have_htm());

	act.sa_sigaction = signal_usr1;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGUSR1, &act, NULL) < 0) {
		perror("sigaction sigusr1");
		exit(1);
	}

	i = 0;
	while (i < MAX_ATTEMPT && !fail) {
		rc = tm_signal_self_context_load(pid, NULL, fps, NULL, NULL);
		FAIL_IF(rc != pid);
		i++;
	}

	return fail;
}

int main(void)
{
	return test_harness(tm_signal_context_chk_fpu, "tm_signal_context_chk_fpu");
}
