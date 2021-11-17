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
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <altivec.h>

#include "utils.h"
#include "tm.h"

#define MAX_ATTEMPT 500000

#define NV_VMX_REGS 12 /* Number of non-volatile VMX registers */
#define VMX20 20 /* First non-volatile register to check in vr20-31 subset */

long tm_signal_self_context_load(pid_t pid, long *gprs, double *fps, vector int *vms, vector int *vss);

static sig_atomic_t fail, broken;

/* Test only non-volatile registers, i.e. 12 vmx registers from vr20 to vr31 */
vector int vms[] = {
	/* First context will be set with these values, i.e. non-speculative */
	/* VMX20     ,  VMX21      , ... */
	{ 1, 2, 3, 4},{ 5, 6, 7, 8},{ 9,10,11,12},
	{13,14,15,16},{17,18,19,20},{21,22,23,24},
	{25,26,27,28},{29,30,31,32},{33,34,35,36},
	{37,38,39,40},{41,42,43,44},{45,46,47,48},
	/* Second context will be set with these values, i.e. speculative */
	/* VMX20        , VMX21            , ... */
	{ -1, -2, -3, -4},{ -5, -6, -7, -8},{ -9,-10,-11,-12},
	{-13,-14,-15,-16},{-17,-18,-19,-20},{-21,-22,-23,-24},
	{-25,-26,-27,-28},{-29,-30,-31,-32},{-33,-34,-35,-36},
	{-37,-38,-39,-40},{-41,-42,-43,-44},{-45,-46,-47,-48}
};

static void signal_usr1(int signum, siginfo_t *info, void *uc)
{
	int i, j;
	ucontext_t *ucp = uc;
	ucontext_t *tm_ucp = ucp->uc_link;

	for (i = 0; i < NV_VMX_REGS; i++) {
		/* Check first context. Print all mismatches. */
		fail = memcmp(ucp->uc_mcontext.v_regs->vrregs[VMX20 + i],
				&vms[i], sizeof(vector int));
		if (fail) {
			broken = 1;
			printf("VMX%d (1st context) == 0x", VMX20 + i);
			/* Print actual value in first context. */
			for (j = 0; j < 4; j++)
				printf("%08x", ucp->uc_mcontext.v_regs->vrregs[VMX20 + i][j]);
			printf(" instead of 0x");
			/* Print expected value. */
			for (j = 0; j < 4; j++)
				printf("%08x", vms[i][j]);
			printf(" (expected)\n");
		}
	}

	for (i = 0; i < NV_VMX_REGS; i++)  {
		/* Check second context. Print all mismatches. */
		fail = memcmp(tm_ucp->uc_mcontext.v_regs->vrregs[VMX20 + i],
				&vms[NV_VMX_REGS + i], sizeof (vector int));
		if (fail) {
			broken = 1;
			printf("VMX%d (2nd context) == 0x", NV_VMX_REGS + i);
			/* Print actual value in second context. */
			for (j = 0; j < 4; j++)
				printf("%08x", tm_ucp->uc_mcontext.v_regs->vrregs[VMX20 + i][j]);
			printf(" instead of 0x");
			/* Print expected value. */
			for (j = 0; j < 4; j++)
				printf("%08x", vms[NV_VMX_REGS + i][j]);
			printf(" (expected)\n");
		}
	}
}

static int tm_signal_context_chk()
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
		 * array pointers to it, in that case 'vms', and invoke the
		 * signal handler installed for SIGUSR1.
		 */
		rc = tm_signal_self_context_load(pid, NULL, NULL, vms, NULL);
		FAIL_IF(rc != pid);
		i++;
	}

	return (broken);
}

int main(void)
{
	return test_harness(tm_signal_context_chk, "tm_signal_context_chk_vmx");
}
