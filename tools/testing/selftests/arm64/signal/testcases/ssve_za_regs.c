// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Verify that both the streaming SVE and ZA register context in
 * signal frames is set up as expected when enabled simultaneously.
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

static void setup_regs(void)
{
	/* smstart sm; real data is TODO */
	asm volatile(".inst 0xd503437f" : : : );

	/* smstart za; real data is TODO */
	asm volatile(".inst 0xd503457f" : : : );
}

static char zeros[ZA_SIG_REGS_SIZE(SVE_VQ_MAX)];

static int do_one_sme_vl(struct tdescr *td, siginfo_t *si, ucontext_t *uc,
			 unsigned int vl)
{
	size_t offset;
	struct _aarch64_ctx *head = GET_BUF_RESV_HEAD(context);
	struct _aarch64_ctx *regs;
	struct sve_context *ssve;
	struct za_context *za;
	int ret;

	fprintf(stderr, "Testing VL %d\n", vl);

	ret = prctl(PR_SME_SET_VL, vl);
	if (ret != vl) {
		fprintf(stderr, "Failed to set VL, got %d\n", ret);
		return 1;
	}

	/*
	 * Get a signal context which should have the SVE and ZA
	 * frames in it.
	 */
	setup_regs();
	if (!get_current_context(td, &context.uc, sizeof(context)))
		return 1;

	regs = get_header(head, SVE_MAGIC, GET_BUF_RESV_SIZE(context),
			  &offset);
	if (!regs) {
		fprintf(stderr, "No SVE context\n");
		return 1;
	}

	ssve = (struct sve_context *)regs;
	if (ssve->vl != vl) {
		fprintf(stderr, "Got SSVE VL %d, expected %d\n", ssve->vl, vl);
		return 1;
	}

	if (!(ssve->flags & SVE_SIG_FLAG_SM)) {
		fprintf(stderr, "SVE_SIG_FLAG_SM not set in SVE record\n");
		return 1;
	}

	fprintf(stderr, "Got expected SSVE size %u and VL %d\n",
		regs->size, ssve->vl);

	regs = get_header(head, ZA_MAGIC, GET_BUF_RESV_SIZE(context),
			  &offset);
	if (!regs) {
		fprintf(stderr, "No ZA context\n");
		return 1;
	}

	za = (struct za_context *)regs;
	if (za->vl != vl) {
		fprintf(stderr, "Got ZA VL %d, expected %d\n", za->vl, vl);
		return 1;
	}

	fprintf(stderr, "Got expected ZA size %u and VL %d\n",
		regs->size, za->vl);

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
	.name = "Streaming SVE registers",
	.descr = "Check that we get the right Streaming SVE registers reported",
	.feats_required = FEAT_SME,
	.timeout = 3,
	.init = sme_get_vls,
	.run = sme_regs,
};
