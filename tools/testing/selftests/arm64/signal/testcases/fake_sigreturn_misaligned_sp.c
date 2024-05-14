// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 ARM Limited
 *
 * Place a fake sigframe on the stack at a misaligned SP: on sigreturn
 * Kernel must spot this attempt and the test case is expected to be
 * terminated via SEGV.
 */

#include <signal.h>
#include <ucontext.h>

#include "test_signals_utils.h"
#include "testcases.h"

struct fake_sigframe sf;

static int fake_sigreturn_misaligned_run(struct tdescr *td,
					 siginfo_t *si, ucontext_t *uc)
{
	/* just to fill the ucontext_t with something real */
	if (!get_current_context(td, &sf.uc, sizeof(sf.uc)))
		return 1;

	/* Forcing sigframe on misaligned SP (16 + 3) */
	fake_sigreturn(&sf, sizeof(sf), 3);

	return 1;
}

struct tdescr tde = {
		.name = "FAKE_SIGRETURN_MISALIGNED_SP",
		.descr = "Triggers a sigreturn with a misaligned sigframe",
		.sig_ok = SIGSEGV,
		.timeout = 3,
		.run = fake_sigreturn_misaligned_run,
};
