// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Verify that the streaming SVE register context in signal frames is
 * set up as expected.
 */

#include <kselftest.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "sve_helpers.h"
#include "testcases.h"

static union {
	ucontext_t uc;
	char buf[1024 * 64];
} context;

static bool sme_get_vls(struct tdescr *td)
{
	int res = sve_fill_vls(VLS_USE_SME, 1);

	if (!res)
		return true;

	if (res == KSFT_SKIP)
		td->result = KSFT_SKIP;

	return false;
}

static void setup_ssve_regs(void)
{
	/* smstart sm; real data is TODO */
	asm volatile(".inst 0xd503437f" : : : );
}

static int do_one_sme_vl(struct tdescr *td, siginfo_t *si, ucontext_t *uc,
			 unsigned int vl)
{
	size_t offset;
	struct _aarch64_ctx *head = GET_BUF_RESV_HEAD(context);
	struct sve_context *ssve;
	int ret;

	fprintf(stderr, "Testing VL %d\n", vl);

	ret = prctl(PR_SME_SET_VL, vl);
	if (ret != vl) {
		fprintf(stderr, "Failed to set VL, got %d\n", ret);
		return 1;
	}

	/*
	 * Get a signal context which should have a SVE frame and registers
	 * in it.
	 */
	setup_ssve_regs();
	if (!get_current_context(td, &context.uc, sizeof(context)))
		return 1;

	head = get_header(head, SVE_MAGIC, GET_BUF_RESV_SIZE(context),
			  &offset);
	if (!head) {
		fprintf(stderr, "No SVE context\n");
		return 1;
	}

	ssve = (struct sve_context *)head;
	if (ssve->vl != vl) {
		fprintf(stderr, "Got VL %d, expected %d\n", ssve->vl, vl);
		return 1;
	}

	if (!(ssve->flags & SVE_SIG_FLAG_SM)) {
		fprintf(stderr, "SVE_SIG_FLAG_SM not set in SVE record\n");
		return 1;
	}

	/* The actual size validation is done in get_current_context() */
	fprintf(stderr, "Got expected size %u and VL %d\n",
		head->size, ssve->vl);

	if (get_svcr() != 0) {
		fprintf(stderr, "Unexpected SVCR %lx\n", get_svcr());
		return 1;
	}

	return 0;
}

static int sme_regs(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	int i;

	for (i = 0; i < nvls; i++) {
		if (do_one_sme_vl(td, si, uc, vls[i]))
			return 1;
	}

	td->pass = 1;

	return 0;
}

struct tdescr tde = {
	.name = "Streaming SVE registers",
	.descr = "Check that we get the right Streaming SVE registers reported",
	.feats_required = FEAT_SME,
	.timeout = 3,
	.init = sme_get_vls,
	.run = sme_regs,
};
