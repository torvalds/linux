// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Arm Limited
 *
 * Verify that the POR_EL0 register context in signal frames is set up as
 * expected.
 */

#include <signal.h>
#include <ucontext.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <asm/sigcontext.h>

#include "test_signals_utils.h"
#include "testcases.h"

static union {
	ucontext_t uc;
	char buf[1024 * 128];
} context;

#define SYS_POR_EL0 "S3_3_C10_C2_4"

static uint64_t get_por_el0(void)
{
	uint64_t val;

	asm volatile(
		"mrs	%0, " SYS_POR_EL0 "\n"
		: "=r"(val)
		:
		: );

	return val;
}

int poe_present(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	struct _aarch64_ctx *head = GET_BUF_RESV_HEAD(context);
	struct poe_context *poe_ctx;
	size_t offset;
	bool in_sigframe;
	bool have_poe;
	__u64 orig_poe;

	have_poe = getauxval(AT_HWCAP2) & HWCAP2_POE;
	if (have_poe)
		orig_poe = get_por_el0();

	if (!get_current_context(td, &context.uc, sizeof(context)))
		return 1;

	poe_ctx = (struct poe_context *)
		get_header(head, POE_MAGIC, td->live_sz, &offset);

	in_sigframe = poe_ctx != NULL;

	fprintf(stderr, "POR_EL0 sigframe %s on system %s POE\n",
		in_sigframe ? "present" : "absent",
		have_poe ? "with" : "without");

	td->pass = (in_sigframe == have_poe);

	/*
	 * Check that the value we read back was the one present at
	 * the time that the signal was triggered.
	 */
	if (have_poe && poe_ctx) {
		if (poe_ctx->por_el0 != orig_poe) {
			fprintf(stderr, "POR_EL0 in frame is %llx, was %llx\n",
				poe_ctx->por_el0, orig_poe);
			td->pass = false;
		}
	}

	return 0;
}

struct tdescr tde = {
	.name = "POR_EL0",
	.descr = "Validate that POR_EL0 is present as expected",
	.timeout = 3,
	.run = poe_present,
};
