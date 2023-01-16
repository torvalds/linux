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

static void enable_za(void)
{
	/* smstart za; real data is TODO */
	asm volatile(".inst 0xd503457f" : : : );
}

int zt_regs_run(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	size_t offset;
	struct _aarch64_ctx *head = GET_BUF_RESV_HEAD(context);
	struct zt_context *zt;
	char *zeros;

	/*
	 * Get a signal context which should have a ZT frame and registers
	 * in it.
	 */
	enable_za();
	if (!get_current_context(td, &context.uc, sizeof(context)))
		return 1;

	head = get_header(head, ZT_MAGIC, GET_BUF_RESV_SIZE(context), &offset);
	if (!head) {
		fprintf(stderr, "No ZT context\n");
		return 1;
	}

	zt = (struct zt_context *)head;
	if (zt->nregs == 0) {
		fprintf(stderr, "Got context with no registers\n");
		return 1;
	}

	fprintf(stderr, "Got expected size %u for %d registers\n",
		head->size, zt->nregs);

	/* We didn't load any data into ZT so it should be all zeros */
	zeros = malloc(ZT_SIG_REGS_SIZE(zt->nregs));
	if (!zeros) {
		fprintf(stderr, "Out of memory, nregs=%u\n", zt->nregs);
		return 1;
	}
	memset(zeros, 0, ZT_SIG_REGS_SIZE(zt->nregs));

	if (memcmp(zeros, (char *)zt + ZT_SIG_REGS_OFFSET,
		   ZT_SIG_REGS_SIZE(zt->nregs)) != 0) {
		fprintf(stderr, "ZT data invalid\n");
		return 1;
	}

	free(zeros);

	td->pass = 1;

	return 0;
}

struct tdescr tde = {
	.name = "ZT register data",
	.descr = "Validate that ZT is present and has data when ZA is enabled",
	.feats_required = FEAT_SME2,
	.timeout = 3,
	.sanity_disabled = true,
	.run = zt_regs_run,
};
