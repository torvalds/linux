// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Verify that both the streaming SVE and ZA register context in
 * signal frames is set up as expected when enabled simultaneously.
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
	char buf[1024 * 128];
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
