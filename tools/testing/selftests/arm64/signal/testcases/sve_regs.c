// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Verify that the SVE register context in signal frames is set up as
 * expected.
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

static bool sve_get_vls(struct tdescr *td)
{
	int vq, vl;

	/*
	 * Enumerate up to SVE_VQ_MAX vector lengths
	 */
	for (vq = SVE_VQ_MAX; vq > 0; --vq) {
		vl = prctl(PR_SVE_SET_VL, vq * 16);
		if (vl == -1)
			return false;

		vl &= PR_SVE_VL_LEN_MASK;

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

static void setup_sve_regs(void)
{
	/* RDVL x16, #1 so we should have SVE regs; real data is TODO */
	asm volatile(".inst 0x04bf5030" : : : "x16" );
}

static int do_one_sve_vl(struct tdescr *td, siginfo_t *si, ucontext_t *uc,
			 unsigned int vl)
{
	size_t offset;
	struct _aarch64_ctx *head = GET_BUF_RESV_HEAD(context);
	struct sve_context *sve;

	fprintf(stderr, "Testing VL %d\n", vl);

	if (prctl(PR_SVE_SET_VL, vl) == -1) {
		fprintf(stderr, "Failed to set VL\n");
		return 1;
	}

	/*
	 * Get a signal context which should have a SVE frame and registers
	 * in it.
	 */
	setup_sve_regs();
	if (!get_current_context(td, &context.uc, sizeof(context)))
		return 1;

	head = get_header(head, SVE_MAGIC, GET_BUF_RESV_SIZE(context),
			  &offset);
	if (!head) {
		fprintf(stderr, "No SVE context\n");
		return 1;
	}

	sve = (struct sve_context *)head;
	if (sve->vl != vl) {
		fprintf(stderr, "Got VL %d, expected %d\n", sve->vl, vl);
		return 1;
	}

	/* The actual size validation is done in get_current_context() */
	fprintf(stderr, "Got expected size %u and VL %d\n",
		head->size, sve->vl);

	return 0;
}

static int sve_regs(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	int i;

	for (i = 0; i < nvls; i++) {
		if (do_one_sve_vl(td, si, uc, vls[i]))
			return 1;
	}

	td->pass = 1;

	return 0;
}

struct tdescr tde = {
	.name = "SVE registers",
	.descr = "Check that we get the right SVE registers reported",
	.feats_required = FEAT_SVE,
	.timeout = 3,
	.init = sve_get_vls,
	.run = sve_regs,
};
