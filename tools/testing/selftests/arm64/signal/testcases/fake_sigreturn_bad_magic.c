// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 ARM Limited
 *
 * Place a fake sigframe on the stack including a BAD Unknown magic
 * record: on sigreturn Kernel must spot this attempt and the test
 * case is expected to be terminated via SEGV.
 */

#include <signal.h>
#include <ucontext.h>

#include "test_signals_utils.h"
#include "testcases.h"

struct fake_sigframe sf;

static int fake_sigreturn_bad_magic_run(struct tdescr *td,
					siginfo_t *si, ucontext_t *uc)
{
	struct _aarch64_ctx *shead = GET_SF_RESV_HEAD(sf), *head;

	/* just to fill the ucontext_t with something real */
	if (!get_current_context(td, &sf.uc, sizeof(sf.uc)))
		return 1;

	/* need at least 2*HDR_SZ space: KSFT_BAD_MAGIC + terminator. */
	head = get_starting_head(shead, HDR_SZ * 2, GET_SF_RESV_SIZE(sf), NULL);
	if (!head)
		return 0;

	/*
	 * use a well known NON existent bad magic...something
	 * we should pretty sure won't be ever defined in Kernel
	 */
	head->magic = KSFT_BAD_MAGIC;
	head->size = HDR_SZ;
	write_terminator_record(GET_RESV_NEXT_HEAD(head));

	ASSERT_BAD_CONTEXT(&sf.uc);
	fake_sigreturn(&sf, sizeof(sf), 0);

	return 1;
}

struct tdescr tde = {
		.name = "FAKE_SIGRETURN_BAD_MAGIC",
		.descr = "Trigger a sigreturn with a sigframe with a bad magic",
		.sig_ok = SIGSEGV,
		.timeout = 3,
		.run = fake_sigreturn_bad_magic_run,
};
