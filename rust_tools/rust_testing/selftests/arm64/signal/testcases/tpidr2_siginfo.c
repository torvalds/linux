// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 ARM Limited
 *
 * Verify that the TPIDR2 register context in signal frames is set up as
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

#define SYS_TPIDR2 "S3_3_C13_C0_5"

static uint64_t get_tpidr2(void)
{
	uint64_t val;

	asm volatile (
		"mrs	%0, " SYS_TPIDR2 "\n"
		: "=r"(val)
		:
		: "cc");

	return val;
}

int tpidr2_present(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	struct _aarch64_ctx *head = GET_BUF_RESV_HEAD(context);
	struct tpidr2_context *tpidr2_ctx;
	size_t offset;
	bool in_sigframe;
	bool have_sme;
	__u64 orig_tpidr2;

	have_sme = getauxval(AT_HWCAP2) & HWCAP2_SME;
	if (have_sme)
		orig_tpidr2 = get_tpidr2();

	if (!get_current_context(td, &context.uc, sizeof(context)))
		return 1;

	tpidr2_ctx = (struct tpidr2_context *)
		get_header(head, TPIDR2_MAGIC, td->live_sz, &offset);

	in_sigframe = tpidr2_ctx != NULL;

	fprintf(stderr, "TPIDR2 sigframe %s on system %s SME\n",
		in_sigframe ? "present" : "absent",
		have_sme ? "with" : "without");

	td->pass = (in_sigframe == have_sme);

	/*
	 * Check that the value we read back was the one present at
	 * the time that the signal was triggered.  TPIDR2 is owned by
	 * libc so we can't safely choose the value and it is possible
	 * that we may need to revisit this in future if something
	 * starts deciding to set a new TPIDR2 between us reading and
	 * the signal.
	 */
	if (have_sme && tpidr2_ctx) {
		if (tpidr2_ctx->tpidr2 != orig_tpidr2) {
			fprintf(stderr, "TPIDR2 in frame is %llx, was %llx\n",
				tpidr2_ctx->tpidr2, orig_tpidr2);
			td->pass = false;
		}
	}

	return 0;
}

struct tdescr tde = {
	.name = "TPIDR2",
	.descr = "Validate that TPIDR2 is present as expected",
	.timeout = 3,
	.run = tpidr2_present,
};
