// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Check that the SVE vector length reported in signal contexts is the
 * expected one.
 */

#include <signal.h>
#include <ucontext.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "testcases.h"

struct fake_sigframe sf;
unsigned int vl;

static bool get_sve_vl(struct tdescr *td)
{
	int ret = prctl(PR_SVE_GET_VL);
	if (ret == -1)
		return false;

	vl = ret;

	return true;
}

static int sve_vl(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	size_t resv_sz, offset;
	struct _aarch64_ctx *head = GET_SF_RESV_HEAD(sf);
	struct sve_context *sve;

	/* Get a signal context which should have a SVE frame in it */
	if (!get_current_context(td, &sf.uc))
		return 1;

	resv_sz = GET_SF_RESV_SIZE(sf);
	head = get_header(head, SVE_MAGIC, resv_sz, &offset);
	if (!head) {
		fprintf(stderr, "No SVE context\n");
		return 1;
	}
	sve = (struct sve_context *)head;

	if (sve->vl != vl) {
		fprintf(stderr, "sigframe VL %u, expected %u\n",
			sve->vl, vl);
		return 1;
	} else {
		fprintf(stderr, "got expected VL %u\n", vl);
	}

	td->pass = 1;

	return 0;
}

struct tdescr tde = {
	.name = "SVE VL",
	.descr = "Check that we get the right SVE VL reported",
	.feats_required = FEAT_SVE,
	.timeout = 3,
	.init = get_sve_vl,
	.run = sve_vl,
};
