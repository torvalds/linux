// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 ARM Limited
 *
 * Verify that the TPIDR2 register context in signal frames is restored.
 */

#include <signal.h>
#include <ucontext.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <asm/sigcontext.h>

#include "test_signals_utils.h"
#include "testcases.h"

#define SYS_TPIDR2 "S3_3_C13_C0_5"

static uint64_t get_tpidr2(void)
{
	uint64_t val;

	asm volatile (
		"mrs	%0, " SYS_TPIDR2 "\n"
		: "=r"(val)
		:
		: "cc");

	return val;
}

static void set_tpidr2(uint64_t val)
{
	asm volatile (
		"msr	" SYS_TPIDR2 ", %0\n"
		:
		: "r"(val)
		: "cc");
}


static uint64_t initial_tpidr2;

static bool save_tpidr2(struct tdescr *td)
{
	initial_tpidr2 = get_tpidr2();
	fprintf(stderr, "Initial TPIDR2: %lx\n", initial_tpidr2);

	return true;
}

static int modify_tpidr2(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	uint64_t my_tpidr2 = get_tpidr2();

	my_tpidr2++;
	fprintf(stderr, "Setting TPIDR2 to %lx\n", my_tpidr2);
	set_tpidr2(my_tpidr2);

	return 0;
}

static void check_tpidr2(struct tdescr *td)
{
	uint64_t tpidr2 = get_tpidr2();

	td->pass = tpidr2 == initial_tpidr2;

	if (td->pass)
		fprintf(stderr, "TPIDR2 restored\n");
	else
		fprintf(stderr, "TPIDR2 was %lx but is now %lx\n",
			initial_tpidr2, tpidr2);
}

struct tdescr tde = {
	.name = "TPIDR2 restore",
	.descr = "Validate that TPIDR2 is restored from the sigframe",
	.feats_required = FEAT_SME,
	.timeout = 3,
	.sig_trig = SIGUSR1,
	.init = save_tpidr2,
	.run = modify_tpidr2,
	.check_result = check_tpidr2,
};
