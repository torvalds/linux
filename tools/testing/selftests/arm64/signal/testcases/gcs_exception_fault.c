// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 ARM Limited
 */

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "testcases.h"

/*
 * We should get this from asm/siginfo.h but the testsuite is being
 * clever with redefining siginfo_t.
 */
#ifndef SEGV_CPERR
#define SEGV_CPERR 10
#endif

static inline void gcsss1(uint64_t Xt)
{
	asm volatile (
		"sys #3, C7, C7, #2, %0\n"
		:
		: "rZ" (Xt)
		: "memory");
}

static int gcs_op_fault_trigger(struct tdescr *td)
{
	/*
	 * The slot below our current GCS should be in a valid GCS but
	 * must not have a valid cap in it.
	 */
	gcsss1(get_gcspr_el0() - 8);

	return 0;
}

static int gcs_op_fault_signal(struct tdescr *td, siginfo_t *si,
				  ucontext_t *uc)
{
	ASSERT_GOOD_CONTEXT(uc);

	return 1;
}

struct tdescr tde = {
	.name = "Invalid GCS operation",
	.descr = "An invalid GCS operation generates the expected signal",
	.feats_required = FEAT_GCS,
	.timeout = 3,
	.sig_ok = SIGSEGV,
	.sig_ok_code = SEGV_CPERR,
	.sanity_disabled = true,
	.trigger = gcs_op_fault_trigger,
	.run = gcs_op_fault_signal,
};
