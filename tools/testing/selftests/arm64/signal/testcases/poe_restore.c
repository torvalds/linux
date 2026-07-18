// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Arm Ltd
 *
 * Verify that the POR_EL0 register is saved and restored as expected on signal
 * entry/return.
 */

#include <asm/sigcontext.h>

#include "test_signals_utils.h"
#include "testcases.h"

#define POR_EL0_INIT	0x07ul
#define POR_EL0_CUSTOM	0x77ul

static bool failed_check;

static bool modify_por_el0(struct tdescr *td)
{
	set_por_el0(POR_EL0_CUSTOM);

	return true;
}

static int signal_check_por_el0_reset(struct tdescr *td, siginfo_t *si,
				      ucontext_t *uc)
{
	uint64_t signal_por_el0 = get_por_el0();

	if (signal_por_el0 != POR_EL0_INIT) {
		fprintf(stderr, "POR_EL0 is %lx in signal handler (expected %lx)\n",
			signal_por_el0, POR_EL0_INIT);
		failed_check = true;
	}

	return 0;
}

static void check_por_el0_restored(struct tdescr *td)
{
	uint64_t por_el0 = get_por_el0();

	if (por_el0 == POR_EL0_CUSTOM) {
		fprintf(stderr, "POR_EL0 restored\n");
	} else {
		fprintf(stderr, "POR_EL0 was %lx but is now %lx\n",
			POR_EL0_CUSTOM, por_el0);
		failed_check = true;
	}

	td->pass = !failed_check;
}

struct tdescr tde = {
	.name = "POR_EL0 restore",
	.descr = "Validate that POR_EL0 is saved/restored on signal entry/return",
	.feats_required = FEAT_POE,
	.timeout = 3,
	.sig_trig = SIGUSR1,
	.init = modify_por_el0,
	.run = signal_check_por_el0_reset,
	.check_result = check_por_el0_restored,
};
