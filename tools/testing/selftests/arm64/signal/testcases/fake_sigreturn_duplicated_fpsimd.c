// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 ARM Limited
 *
 * Place a fake sigframe on the stack including an additional FPSIMD
 * record: on sigreturn Kernel must spot this attempt and the test
 * case is expected to be terminated via SEGV.
 */

#include <signal.h>
#include <ucontext.h>

#include "test_signals_utils.h"
#include "testcases.h"

struct fake_sigframe sf;

static int fake_sigreturn_duplicated_fpsimd_run(struct tdescr *td,
						siginfo_t *si, ucontext_t *uc)
{
	struct _aarch64_ctx *shead = GET_SF_RESV_HEAD(sf), *head;

	/* just to fill the ucontext_t with something real */
	if (!get_current_context(td, &sf.uc))
		return 1;

	head = get_starting_head(shead, sizeof(struct fpsimd_context) + HDR_SZ,
				 GET_SF_RESV_SIZE(sf), NULL);
	if (!head)
		return 0;

	/* Add a spurious fpsimd_context */
	head->magic = FPSIMD_MAGIC;
	head->size = sizeof(struct fpsimd_context);
	/* and terminate */
	write_terminator_record(GET_RESV_NEXT_HEAD(head));

	ASSERT_BAD_CONTEXT(&sf.uc);
	fake_sigreturn(&sf, sizeof(sf), 0);

	return 1;
}

struct tdescr tde = {
		.name = "FAKE_SIGRETURN_DUPLICATED_FPSIMD",
		.descr = "Triggers a sigreturn including two fpsimd_context",
		.sig_ok = SIGSEGV,
		.timeout = 3,
		.run = fake_sigreturn_duplicated_fpsimd_run,
};
