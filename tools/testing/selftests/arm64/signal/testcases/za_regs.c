// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Verify that the ZA register context in signal frames is set up as
 * expected.
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
static unsigned int vls[SVE_VQ_MAX];
unsigned int nvls = 0;

static bool sme_get_vls(struct tdescr *td)
{
	int vq, vl;

	/*
	 * Enumerate up to SME_VQ_MAX vector lengths
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

static void setup_za_regs(void)
{
	/* smstart za; real data is TODO */
	asm volatile(".inst 0xd503457f" : : : );
}

static char zeros[ZA_SIG_REGS_SIZE(SVE_VQ_MAX)];

static int do_one_sme_vl(struct tdescr *td, siginfo_t *si, ucontext_t *uc,
			 unsigned int vl)
{
	size_t offset;
	struct _aarch64_ctx *head = GET_BUF_RESV_HEAD(context);
	struct za_context *za;

	fprintf(stderr, "Testing VL %d\n", vl);

	if (prctl(PR_SME_SET_VL, vl) != vl) {
		fprintf(stderr, "Failed to set VL\n");
		return 1;
	}

	/*
	 * Get a signal context which should have a SVE frame and registers
	 * in it.
	 */
	setup_za_regs();
	if (!get_current_context(td, &context.uc, sizeof(context)))
		return 1;

	head = get_header(head, ZA_MAGIC, GET_BUF_RESV_SIZE(context), &offset);
	if (!head) {
		fprintf(stderr, "No ZA context\n");
		return 1;
	}

	za = (struct za_context *)head;
	if (za->vl != vl) {
		fprintf(stderr, "Got VL %d, expected %d\n", za->vl, vl);
		return 1;
	}

	if (head->size != ZA_SIG_CONTEXT_SIZE(sve_vq_from_vl(vl))) {
		fprintf(stderr, "ZA context size %u, expected %lu\n",
			head->size, ZA_SIG_CONTEXT_SIZE(sve_vq_from_vl(vl)));
		return 1;
	}

	fprintf(stderr, "Got expected size %u and VL %d\n",
		head->size, za->vl);

	/* We didn't load any data into ZA so it should be all zeros */
	if (memcmp(zeros, (char *)za + ZA_SIG_REGS_OFFSET,
		   ZA_SIG_REGS_SIZE(sve_vq_from_vl(za->vl))) != 0) {
		fprintf(stderr, "ZA data invalid\n");
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
	.name = "ZA register",
	.descr = "Check that we get the right ZA registers reported",
	.feats_required = FEAT_SME,
	.timeout = 3,
	.init = sme_get_vls,
	.run = sme_regs,
};
