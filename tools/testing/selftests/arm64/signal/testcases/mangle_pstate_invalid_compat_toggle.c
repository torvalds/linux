// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 ARM Limited
 *
 * Try to mangle the ucontext from inside a signal handler, toggling
 * the execution state bit: this attempt must be spotted by Kernel and
 * the test case is expected to be terminated via SEGV.
 */

#include "test_signals_utils.h"
#include "testcases.h"

static int mangle_invalid_pstate_run(struct tdescr *td, siginfo_t *si,
				     ucontext_t *uc)
{
	ASSERT_GOOD_CONTEXT(uc);

	/* This config should trigger a SIGSEGV by Kernel */
	uc->uc_mcontext.pstate ^= PSR_MODE32_BIT;

	return 1;
}

struct tdescr tde = {
		.sanity_disabled = true,
		.name = "MANGLE_PSTATE_INVALID_STATE_TOGGLE",
		.descr = "Mangling uc_mcontext with INVALID STATE_TOGGLE",
		.sig_trig = SIGUSR1,
		.sig_ok = SIGSEGV,
		.run = mangle_invalid_pstate_run,
};
