// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Verify that the streaming SVE register context in signal frames is
 * set up as expected.
 */

#include <signal.h>
#include <ucontext.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "testcases.h"

static union {
	ucontext_t uc;
	char buf[1024 * 64];
} context;
static unsigned int vls[SVE_VQ_MAX];
unsigned int nvls = 0;

static bool sme_get_vls(struct tdescr *td)
{
	int vq, vl;

	/*
	 * Enumerate up to SVE_VQ_MAX vector lengths
	 */
	for (vq = SVE_VQ_MAX; vq > 0; --vq) {
		vl = prctl(PR_SME_SET_VL, vq * 16);
		if (vl == -1)
			return false;

		vl &= PR_SME_VL_LEN_MASK;

		/* Did we find the lowest supported VL? */
		if (vq < sve_vq_from_vl(vl))
			break;

		/* Skip missing VLs */
		vq = sve_vq_from_vl(vl);

		vls[nvls++] = vl;
	}

	/* We need at least one VL */
	if (nvls < 1) {
		fprintf(stderr, "Only %d VL supported\n", nvls);
		return false;
	}

	return true;
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

	/* The actual size validation is done in get_current_context() */
	fprintf(stderr, "Got expected size %u and VL %d\n",
		head->size, ssve->vl);

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
	/*
	 * We shouldn't require FA64 but things like memset() used in the
	 * helpers might use unsupported instructions so for now disable
	 * the test unless we've got the full instruction set.
	 */
	.feats_required = FEAT_SME | FEAT_SME_FA64,
	.timeout = 3,
	.init = sme_get_vls,
	.run = sme_regs,
};
