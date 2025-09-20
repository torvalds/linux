// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 ARM Limited
 *
 * Place a fake sigframe on the stack including a badly sized terminator
 * record: on sigreturn Kernel must spot this attempt and the test case
 * is expected to be terminated via SEGV.
 */

#include <signal.h>
#include <ucontext.h>

#include "test_signals_utils.h"
#include "testcases.h"

struct fake_sigframe sf;

static int fake_sigreturn_bad_size_for_magic0_run(struct tdescr *td,
						  siginfo_t *si, ucontext_t *uc)
{
	struct _aarch64_ctx *shead = GET_SF_RESV_HEAD(sf), *head;

	/* just to fill the ucontext_t with something real */
	if (!get_current_context(td, &sf.uc, sizeof(sf.uc)))
		return 1;

	/* at least HDR_SZ for the badly sized terminator. */
	head = get_starting_head(shead, HDR_SZ, GET_SF_RESV_SIZE(sf), NULL);
	if (!head)
		return 0;

	head->magic = 0;
	head->size = HDR_SZ;
	ASSERT_BAD_CONTEXT(&sf.uc);
	fake_sigreturn(&sf, sizeof(sf), 0);

	return 1;
}

struct tdescr tde = {
		.name = "FAKE_SIGRETURN_BAD_SIZE_FOR_TERMINATOR",
		.descr = "Trigger a sigreturn using non-zero size terminator",
		.sig_ok = SIGSEGV,
		.timeout = 3,
		.run = fake_sigreturn_bad_size_for_magic0_run,
};
