// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Check that the SME vector length reported in signal contexts is the
 * expected one.
 */

#include <signal.h>
#include <ucontext.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "testcases.h"

struct fake_sigframe sf;
unsigned int vl;

static bool get_sme_vl(struct tdescr *td)
{
	int ret = prctl(PR_SME_GET_VL);
	if (ret == -1)
		return false;

	vl = ret;

	return true;
}

static int sme_vl(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	size_t resv_sz, offset;
	struct _aarch64_ctx *head = GET_SF_RESV_HEAD(sf);
	struct za_context *za;

	/* Get a signal context which should have a ZA frame in it */
	if (!get_current_context(td, &sf.uc))
		return 1;

	resv_sz = GET_SF_RESV_SIZE(sf);
	head = get_header(head, ZA_MAGIC, resv_sz, &offset);
	if (!head) {
		fprintf(stderr, "No ZA context\n");
		return 1;
	}
	za = (struct za_context *)head;

	if (za->vl != vl) {
		fprintf(stderr, "ZA sigframe VL %u, expected %u\n",
			za->vl, vl);
		return 1;
	} else {
		fprintf(stderr, "got expected VL %u\n", vl);
	}

	td->pass = 1;

	return 0;
}

struct tdescr tde = {
	.name = "SME VL",
	.descr = "Check that we get the right SME VL reported",
	.feats_required = FEAT_SME,
	.timeout = 3,
	.init = get_sme_vl,
	.run = sme_vl,
};
