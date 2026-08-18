// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Arm Ltd
 *
 * Verify that the POR_EL0 register is left untouched on sigreturn if the
 * POE frame record is missing.
 */

#include <asm/sigcontext.h>

#include "test_signals_utils.h"
#include "testcases.h"

#define POR_EL0_INIT	0x07ul
#define POR_EL0_CUSTOM	0x77ul

static bool failed_check;

static bool modify_por_el0(struct tdescr *td)
{
	set_por_el0(POR_EL0_CUSTOM);

	return true;
}

static int signal_remove_poe_context(struct tdescr *td, siginfo_t *si,
				      ucontext_t *uc)
{
	struct _aarch64_ctx *ctx = GET_UC_RESV_HEAD(uc);
	size_t resv_size = GET_UCP_RESV_SIZE(uc);
	struct _aarch64_ctx *poe_ctx_head;

	poe_ctx_head = get_header(ctx, POE_MAGIC, resv_size, NULL);
	if (!poe_ctx_head) {
		fprintf(stderr, "Missing poe_context record\n");
		failed_check = true;
		return 0;
	}

	/*
	 * Actually removing the record would require moving down the next
	 * records. An easier option is to turn it into an ESR record, which is
	 * ignored by sigreturn().
	 */
	poe_ctx_head->magic = ESR_MAGIC;

	return 0;
}

static void check_por_el0_preserved(struct tdescr *td)
{
	uint64_t por_el0 = get_por_el0();

	if (por_el0 == POR_EL0_INIT) {
		fprintf(stderr, "POR_EL0 preserved\n");
	} else {
		fprintf(stderr, "POR_EL0 unexpectedly set to %lx\n", por_el0);
		failed_check = true;
	}

	td->pass = !failed_check;
}

struct tdescr tde = {
	.name = "POR_EL0 missing poe_context",
	.descr = "Remove poe_context record and check POR_EL0 is preserved",
	.feats_required = FEAT_POE,
	.timeout = 3,
	.sig_trig = SIGUSR1,
	.init = modify_por_el0,
	.run = signal_remove_poe_context,
	.check_result = check_por_el0_preserved,
};
