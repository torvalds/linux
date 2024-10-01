// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2016, Cyril Bur, IBM Corp.
 *
 * Test the kernel's signal frame code.
 *
 * The kernel sets up two sets of ucontexts if the signal was to be
 * delivered while the thread was in a transaction (referred too as
 * first and second contexts).
 * Expected behaviour is that the checkpointed state is in the user
 * context passed to the signal handler (first context). The speculated
 * state can be accessed with the uc_link pointer (second context).
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

#define NV_GPR_REGS 18 /* Number of non-volatile GPR registers */
#define R14 14 /* First non-volatile register to check in r14-r31 subset */

long tm_signal_self_context_load(pid_t pid, long *gprs, double *fps, vector int *vms, vector int *vss);

static sig_atomic_t fail, broken;

/* Test only non-volatile general purpose registers, i.e. r14-r31 */
static long gprs[] = {
	/* First context will be set with these values, i.e. non-speculative */
	/* R14, R15, ... */
	 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
	/* Second context will be set with these values, i.e. speculative */
	/* R14, R15, ... */
	-1,-2,-3,-4,-5,-6,-7,-8,-9,-10,-11,-12,-13,-14,-15,-16,-17,-18
};

static void signal_usr1(int signum, siginfo_t *info, void *uc)
{
	int i;
	ucontext_t *ucp = uc;
	ucontext_t *tm_ucp = ucp->uc_link;

	/* Check first context. Print all mismatches. */
	for (i = 0; i < NV_GPR_REGS; i++) {
		fail = (ucp->uc_mcontext.gp_regs[R14 + i] != gprs[i]);
		if (fail) {
			broken = 1;
			printf("GPR%d (1st context) == %lu instead of %lu (expected)\n",
				R14 + i, ucp->uc_mcontext.gp_regs[R14 + i], gprs[i]);
		}
	}

	/* Check second context. Print all mismatches. */
	for (i = 0; i < NV_GPR_REGS; i++) {
		fail = (tm_ucp->uc_mcontext.gp_regs[R14 + i] != gprs[NV_GPR_REGS + i]);
		if (fail) {
			broken = 1;
			printf("GPR%d (2nd context) == %lu instead of %lu (expected)\n",
				R14 + i, tm_ucp->uc_mcontext.gp_regs[R14 + i], gprs[NV_GPR_REGS + i]);
		}
	}
}

static int tm_signal_context_chk_gpr()
{
	struct sigaction act;
	int i;
	long rc;
	pid_t pid = getpid();

	SKIP_IF(!have_htm());
	SKIP_IF(htm_is_synthetic());

	act.sa_sigaction = signal_usr1;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGUSR1, &act, NULL) < 0) {
		perror("sigaction sigusr1");
		exit(1);
	}

	i = 0;
	while (i < MAX_ATTEMPT && !broken) {
                /*
                 * tm_signal_self_context_load will set both first and second
                 * contexts accordingly to the values passed through non-NULL
                 * array pointers to it, in that case 'gprs', and invoke the
                 * signal handler installed for SIGUSR1.
                 */
		rc = tm_signal_self_context_load(pid, gprs, NULL, NULL, NULL);
		FAIL_IF(rc != pid);
		i++;
	}

	return broken;
}

int main(void)
{
	return test_harness(tm_signal_context_chk_gpr, "tm_signal_context_chk_gpr");
}
