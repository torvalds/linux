// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Verify that accessing ZA without enabling it generates a SIGILL.
 */

#include <signal.h>
#include <ucontext.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "testcases.h"

int sme_trap_za_trigger(struct tdescr *td)
{
	/* ZERO ZA */
	asm volatile(".inst 0xc00800ff");

	return 0;
}

int sme_trap_za_run(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	return 1;
}

struct tdescr tde = {
	.name = "SME ZA trap",
	.descr = "Check that we get a SIGILL if we access ZA without enabling",
	.timeout = 3,
	.sanity_disabled = true,
	.trigger = sme_trap_za_trigger,
	.run = sme_trap_za_run,
	.sig_ok = SIGILL,
};
