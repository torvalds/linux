// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2016, Cyril Bur, IBM Corp.
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
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <altivec.h>

#include "utils.h"
#include "tm.h"

#define MAX_ATTEMPT 500000

#define NV_VSX_REGS 12

long tm_signal_self_context_load(pid_t pid, long *gprs, double *fps, vector int *vms, vector int *vss);

static sig_atomic_t fail;

vector int vss[] = {
	{1, 2, 3, 4 },{5, 6, 7, 8 },{9, 10,11,12},
	{13,14,15,16},{17,18,19,20},{21,22,23,24},
	{25,26,27,28},{29,30,31,32},{33,34,35,36},
	{37,38,39,40},{41,42,43,44},{45,46,47,48},
	{-1, -2, -3, -4 },{-5, -6, -7, -8 },{-9, -10,-11,-12},
	{-13,-14,-15,-16},{-17,-18,-19,-20},{-21,-22,-23,-24},
	{-25,-26,-27,-28},{-29,-30,-31,-32},{-33,-34,-35,-36},
	{-37,-38,-39,-40},{-41,-42,-43,-44},{-45,-46,-47,-48}
};

static void signal_usr1(int signum, siginfo_t *info, void *uc)
{
	int i;
	uint8_t vsc[sizeof(vector int)];
	uint8_t vst[sizeof(vector int)];
	ucontext_t *ucp = uc;
	ucontext_t *tm_ucp = ucp->uc_link;

	/*
	 * The other half of the VSX regs will be after v_regs.
	 *
	 * In short, vmx_reserve array holds everything. v_regs is a 16
	 * byte aligned pointer at the start of vmx_reserve (vmx_reserve
	 * may or may not be 16 aligned) where the v_regs structure exists.
	 * (half of) The VSX regsters are directly after v_regs so the
	 * easiest way to find them below.
	 */
	long *vsx_ptr = (long *)(ucp->uc_mcontext.v_regs + 1);
	long *tm_vsx_ptr = (long *)(tm_ucp->uc_mcontext.v_regs + 1);
	for (i = 0; i < NV_VSX_REGS && !fail; i++) {
		memcpy(vsc, &ucp->uc_mcontext.fp_regs[i + 20], 8);
		memcpy(vsc + 8, &vsx_ptr[20 + i], 8);
		fail = memcmp(vsc, &vss[i], sizeof(vector int));
		memcpy(vst, &tm_ucp->uc_mcontext.fp_regs[i + 20], 8);
		memcpy(vst + 8, &tm_vsx_ptr[20 + i], 8);
		fail |= memcmp(vst, &vss[i + NV_VSX_REGS], sizeof(vector int));

		if (fail) {
			int j;

			fprintf(stderr, "Failed on %d vsx 0x", i);
			for (j = 0; j < 16; j++)
				fprintf(stderr, "%02x", vsc[j]);
			fprintf(stderr, " vs 0x");
			for (j = 0; j < 16; j++)
				fprintf(stderr, "%02x", vst[j]);
			fprintf(stderr, "\n");
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

	act.sa_sigaction = signal_usr1;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGUSR1, &act, NULL) < 0) {
		perror("sigaction sigusr1");
		exit(1);
	}

	i = 0;
	while (i < MAX_ATTEMPT && !fail) {
		rc = tm_signal_self_context_load(pid, NULL, NULL, NULL, vss);
		FAIL_IF(rc != pid);
		i++;
	}

	return fail;
}

int main(void)
{
	return test_harness(tm_signal_context_chk, "tm_signal_context_chk_vsx");
}
