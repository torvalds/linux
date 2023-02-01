// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Verify that using an instruction not supported in streaming mode
 * traps when in streaming mode.
 */

#include <signal.h>
#include <ucontext.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "testcases.h"

static union {
	ucontext_t uc;
	char buf[1024 * 128];
} context;

int zt_no_regs_run(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	size_t offset;
	struct _aarch64_ctx *head = GET_BUF_RESV_HEAD(context);

	/*
	 * Get a signal context which should not have a ZT frame and
	 * registers in it.
	 */
	if (!get_current_context(td, &context.uc, sizeof(context)))
		return 1;

	head = get_header(head, ZT_MAGIC, GET_BUF_RESV_SIZE(context), &offset);
	if (head) {
		fprintf(stderr, "Got unexpected ZT context\n");
		return 1;
	}

	td->pass = 1;

	return 0;
}

struct tdescr tde = {
	.name = "ZT register data not present",
	.descr = "Validate that ZT is not present when ZA is disabled",
	.feats_required = FEAT_SME2,
	.timeout = 3,
	.sanity_disabled = true,
	.run = zt_no_regs_run,
};
