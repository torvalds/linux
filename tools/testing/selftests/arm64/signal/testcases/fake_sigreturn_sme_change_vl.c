// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Attempt to change the streaming SVE vector length in a signal
 * handler, this is not supported and is expected to segfault.
 */

#include <signal.h>
#include <ucontext.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "testcases.h"

struct fake_sigframe sf;
static unsigned int vls[SVE_VQ_MAX];
unsigned int nvls = 0;

static bool sme_get_vls(struct tdescr *td)
{
	int vq, vl;

	/*
	 * Enumerate up to SVE_VQ_MAX vector lengths
	 */
	for (vq = SVE_VQ_MAX; vq > 0; --vq) {
		vl = prctl(PR_SVE_SET_VL, vq * 16);
		if (vl == -1)
			return false;

		vl &= PR_SME_VL_LEN_MASK;

		/* Skip missing VLs */
		vq = sve_vq_from_vl(vl);

		vls[nvls++] = vl;
	}

	/* We need at least two VLs */
	if (nvls < 2) {
		fprintf(stderr, "Only %d VL supported\n", nvls);
		return false;
	}

	return true;
}

static int fake_sigreturn_ssve_change_vl(struct tdescr *td,
					 siginfo_t *si, ucontext_t *uc)
{
	size_t resv_sz, offset;
	struct _aarch64_ctx *head = GET_SF_RESV_HEAD(sf);
	struct sve_context *sve;

	/* Get a signal context with a SME ZA frame in it */
	if (!get_current_context(td, &sf.uc, sizeof(sf.uc)))
		return 1;

	resv_sz = GET_SF_RESV_SIZE(sf);
	head = get_header(head, SVE_MAGIC, resv_sz, &offset);
	if (!head) {
		fprintf(stderr, "No SVE context\n");
		return 1;
	}

	if (head->size != sizeof(struct sve_context)) {
		fprintf(stderr, "Register data present, aborting\n");
		return 1;
	}

	sve = (struct sve_context *)head;

	/* No changes are supported; init left us at minimum VL so go to max */
	fprintf(stderr, "Attempting to change VL from %d to %d\n",
		sve->vl, vls[0]);
	sve->vl = vls[0];

	fake_sigreturn(&sf, sizeof(sf), 0);

	return 1;
}

struct tdescr tde = {
	.name = "FAKE_SIGRETURN_SSVE_CHANGE",
	.descr = "Attempt to change Streaming SVE VL",
	.feats_required = FEAT_SME,
	.sig_ok = SIGSEGV,
	.timeout = 3,
	.init = sme_get_vls,
	.run = fake_sigreturn_ssve_change_vl,
};
