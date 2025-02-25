// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 ARM Limited
 *
 * Attempt to change the SVE vector length in a signal hander, this is not
 * supported and is expected to segfault.
 */

#include <kselftest.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/prctl.h>

#include "test_signals_utils.h"
#include "sve_helpers.h"
#include "testcases.h"

struct fake_sigframe sf;

static bool sve_get_vls(struct tdescr *td)
{
	int res = sve_fill_vls(VLS_USE_SVE, 2);

	if (!res)
		return true;

	if (res == KSFT_SKIP)
		td->result = KSFT_SKIP;

	return false;
}

static int fake_sigreturn_sve_change_vl(struct tdescr *td,
					siginfo_t *si, ucontext_t *uc)
{
	size_t resv_sz, offset;
	struct _aarch64_ctx *head = GET_SF_RESV_HEAD(sf);
	struct sve_context *sve;

	/* Get a signal context with a SVE frame in it */
	if (!get_current_context(td, &sf.uc, sizeof(sf.uc)))
		return 1;

	resv_sz = GET_SF_RESV_SIZE(sf);
	head = get_header(head, SVE_MAGIC, resv_sz, &offset);
	if (!head) {
		fprintf(stderr, "No SVE context\n");
		return 1;
	}

	if (head->size != sizeof(struct sve_context)) {
		fprintf(stderr, "SVE register state active, skipping\n");
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
	.name = "FAKE_SIGRETURN_SVE_CHANGE",
	.descr = "Attempt to change SVE VL",
	.feats_required = FEAT_SVE,
	.sig_ok = SIGSEGV,
	.timeout = 3,
	.init = sve_get_vls,
	.run = fake_sigreturn_sve_change_vl,
};
