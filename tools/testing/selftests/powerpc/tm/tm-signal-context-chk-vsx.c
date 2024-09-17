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

#define NV_VSX_REGS 12 /* Number of VSX registers to check. */
#define VSX20 20 /* First VSX register to check in vsr20-vsr31 subset */
#define FPR20 20 /* FPR20 overlaps VSX20 most significant doubleword */

long tm_signal_self_context_load(pid_t pid, long *gprs, double *fps, vector int *vms, vector int *vss);

static sig_atomic_t fail, broken;

/* Test only 12 vsx registers from vsr20 to vsr31 */
vector int vsxs[] = {
	/* First context will be set with these values, i.e. non-speculative */
	/* VSX20     ,  VSX21      , ... */
	{ 1, 2, 3, 4},{ 5, 6, 7, 8},{ 9,10,11,12},
	{13,14,15,16},{17,18,19,20},{21,22,23,24},
	{25,26,27,28},{29,30,31,32},{33,34,35,36},
	{37,38,39,40},{41,42,43,44},{45,46,47,48},
	/* Second context will be set with these values, i.e. speculative */
	/* VSX20         ,  VSX21          , ... */
	{-1, -2, -3, -4 },{-5, -6, -7, -8 },{-9, -10,-11,-12},
	{-13,-14,-15,-16},{-17,-18,-19,-20},{-21,-22,-23,-24},
	{-25,-26,-27,-28},{-29,-30,-31,-32},{-33,-34,-35,-36},
	{-37,-38,-39,-40},{-41,-42,-43,-44},{-45,-46,-47,-48}
};

static void signal_usr1(int signum, siginfo_t *info, void *uc)
{
	int i, j;
	uint8_t vsx[sizeof(vector int)];
	uint8_t vsx_tm[sizeof(vector int)];
	ucontext_t *ucp = uc;
	ucontext_t *tm_ucp = ucp->uc_link;

	/*
	 * FP registers and VMX registers overlap the VSX registers.
	 *
	 * FP registers (f0-31) overlap the most significant 64 bits of VSX
	 * registers vsr0-31, whilst VMX registers vr0-31, being 128-bit like
	 * the VSX registers, overlap fully the other half of VSX registers,
	 * i.e. vr0-31 overlaps fully vsr32-63.
	 *
	 * Due to compatibility and historical reasons (VMX/Altivec support
	 * appeared first on the architecture), VMX registers vr0-31 (so VSX
	 * half vsr32-63 too) are stored right after the v_regs pointer, in an
	 * area allocated for 'vmx_reverse' array (please see
	 * arch/powerpc/include/uapi/asm/sigcontext.h for details about the
	 * mcontext_t structure on Power).
	 *
	 * The other VSX half (vsr0-31) is hence stored below vr0-31/vsr32-63
	 * registers, but only the least significant 64 bits of vsr0-31. The
	 * most significant 64 bits of vsr0-31 (f0-31), as it overlaps the FP
	 * registers, is kept in fp_regs.
	 *
	 * v_regs is a 16 byte aligned pointer at the start of vmx_reserve
	 * (vmx_reserve may or may not be 16 aligned) where the v_regs structure
	 * exists, so v_regs points to where vr0-31 / vsr32-63 registers are
	 * fully stored. Since v_regs type is elf_vrregset_t, v_regs + 1
	 * skips all the slots used to store vr0-31 / vsr32-64 and points to
	 * part of one VSX half, i.e. v_regs + 1 points to the least significant
	 * 64 bits of vsr0-31. The other part of this half (the most significant
	 * part of vsr0-31) is stored in fp_regs.
	 *
	 */
	/* Get pointer to least significant doubleword of vsr0-31 */
	long *vsx_ptr = (long *)(ucp->uc_mcontext.v_regs + 1);
	long *tm_vsx_ptr = (long *)(tm_ucp->uc_mcontext.v_regs + 1);

	/* Check first context. Print all mismatches. */
	for (i = 0; i < NV_VSX_REGS; i++) {
		/*
		 * Copy VSX most significant doubleword from fp_regs and
		 * copy VSX least significant one from 64-bit slots below
		 * saved VMX registers.
		 */
		memcpy(vsx, &ucp->uc_mcontext.fp_regs[FPR20 + i], 8);
		memcpy(vsx + 8, &vsx_ptr[VSX20 + i], 8);

		fail = memcmp(vsx, &vsxs[i], sizeof(vector int));

		if (fail) {
			broken = 1;
			printf("VSX%d (1st context) == 0x", VSX20 + i);
			for (j = 0; j < 16; j++)
				printf("%02x", vsx[j]);
			printf(" instead of 0x");
			for (j = 0; j < 4; j++)
				printf("%08x", vsxs[i][j]);
			printf(" (expected)\n");
		}
	}

	/* Check second context. Print all mismatches. */
	for (i = 0; i < NV_VSX_REGS; i++) {
		/*
		 * Copy VSX most significant doubleword from fp_regs and
		 * copy VSX least significant one from 64-bit slots below
		 * saved VMX registers.
		 */
		memcpy(vsx_tm, &tm_ucp->uc_mcontext.fp_regs[FPR20 + i], 8);
		memcpy(vsx_tm + 8, &tm_vsx_ptr[VSX20 + i], 8);

		fail = memcmp(vsx_tm, &vsxs[NV_VSX_REGS + i], sizeof(vector int));

		if (fail) {
			broken = 1;
			printf("VSX%d (2nd context) == 0x", VSX20 + i);
			for (j = 0; j < 16; j++)
				printf("%02x", vsx_tm[j]);
			printf(" instead of 0x");
			for (j = 0; j < 4; j++)
				printf("%08x", vsxs[NV_VSX_REGS + i][j]);
			printf("(expected)\n");
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
                * array pointers to it, in that case 'vsxs', and invoke the
                * signal handler installed for SIGUSR1.
                */
		rc = tm_signal_self_context_load(pid, NULL, NULL, NULL, vsxs);
		FAIL_IF(rc != pid);
		i++;
	}

	return (broken);
}

int main(void)
{
	return test_harness(tm_signal_context_chk, "tm_signal_context_chk_vsx");
}
