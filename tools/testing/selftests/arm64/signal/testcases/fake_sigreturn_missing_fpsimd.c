// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 ARM Limited
 *
 * Place a fake sigframe on the stack missing the mandatory FPSIMD
 * record: on sigreturn Kernel must spot this attempt and the test
 * case is expected to be terminated via SEGV.
 */

#include <stdio.h>
#include <signal.h>
#include <ucontext.h>

#include "test_signals_utils.h"
#include "testcases.h"

struct fake_sigframe sf;

static int fake_sigreturn_missing_fpsimd_run(struct tdescr *td,
					     siginfo_t *si, ucontext_t *uc)
{
	size_t resv_sz, offset;
	struct _aarch64_ctx *head = GET_SF_RESV_HEAD(sf);

	/* just to fill the ucontext_t with something real */
	if (!get_current_context(td, &sf.uc, sizeof(sf.uc)))
		return 1;

	resv_sz = GET_SF_RESV_SIZE(sf);
	head = get_header(head, FPSIMD_MAGIC, resv_sz, &offset);
	if (head && resv_sz - offset >= HDR_SZ) {
		fprintf(stderr, "Mangling template header. Spare space:%zd\n",
			resv_sz - offset);
		/* Just overwrite fpsmid_context */
		write_terminator_record(head);

		ASSERT_BAD_CONTEXT(&sf.uc);
		fake_sigreturn(&sf, sizeof(sf), 0);
	}

	return 1;
}

struct tdescr tde = {
		.name = "FAKE_SIGRETURN_MISSING_FPSIMD",
		.descr = "Triggers a sigreturn with a missing fpsimd_context",
		.sig_ok = SIGSEGV,
		.timeout = 3,
		.run = fake_sigreturn_missing_fpsimd_run,
};
