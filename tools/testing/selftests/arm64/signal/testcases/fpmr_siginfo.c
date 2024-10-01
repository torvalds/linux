// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 ARM Limited
 *
 * Verify that the FPMR register context in signal frames is set up as
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

#define SYS_FPMR "S3_3_C4_C4_2"

static uint64_t get_fpmr(void)
{
	uint64_t val;

	asm volatile (
		"mrs	%0, " SYS_FPMR "\n"
		: "=r"(val)
		:
		: "cc");

	return val;
}

int fpmr_present(struct tdescr *td, siginfo_t *si, ucontext_t *uc)
{
	struct _aarch64_ctx *head = GET_BUF_RESV_HEAD(context);
	struct fpmr_context *fpmr_ctx;
	size_t offset;
	bool in_sigframe;
	bool have_fpmr;
	__u64 orig_fpmr;

	have_fpmr = getauxval(AT_HWCAP2) & HWCAP2_FPMR;
	if (have_fpmr)
		orig_fpmr = get_fpmr();

	if (!get_current_context(td, &context.uc, sizeof(context)))
		return 1;

	fpmr_ctx = (struct fpmr_context *)
		get_header(head, FPMR_MAGIC, td->live_sz, &offset);

	in_sigframe = fpmr_ctx != NULL;

	fprintf(stderr, "FPMR sigframe %s on system %s FPMR\n",
		in_sigframe ? "present" : "absent",
		have_fpmr ? "with" : "without");

	td->pass = (in_sigframe == have_fpmr);

	if (have_fpmr && fpmr_ctx) {
		if (fpmr_ctx->fpmr != orig_fpmr) {
			fprintf(stderr, "FPMR in frame is %llx, was %llx\n",
				fpmr_ctx->fpmr, orig_fpmr);
			td->pass = false;
		}
	}

	return 0;
}

struct tdescr tde = {
	.name = "FPMR",
	.descr = "Validate that FPMR is present as expected",
	.timeout = 3,
	.run = fpmr_present,
};
