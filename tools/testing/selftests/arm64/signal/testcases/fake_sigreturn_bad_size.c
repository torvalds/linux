// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 ARM Limited
 *
 * Place a fake sigframe on the stack including a bad record overflowing
 * the __reserved space: on sigreturn Kernel must spot this attempt and
 * the test case is expected to be terminated via SEGV.
 */

#include <signal.h>
#include <ucontext.h>

#include "test_signals_utils.h"
#include "testcases.h"

struct fake_sigframe sf;

#define MIN_SZ_ALIGN	16

static int fake_sigreturn_bad_size_run(struct tdescr *td,
				       siginfo_t *si, ucontext_t *uc)
{
	size_t resv_sz, need_sz, offset;
	struct _aarch64_ctx *shead = GET_SF_RESV_HEAD(sf), *head;

	/* just to fill the ucontext_t with something real */
	if (!get_current_context(td, &sf.uc))
		return 1;

	resv_sz = GET_SF_RESV_SIZE(sf);
	/* at least HDR_SZ + bad sized esr_context needed */
	need_sz = sizeof(struct esr_context) + HDR_SZ;
	head = get_starting_head(shead, need_sz, resv_sz, &offset);
	if (!head)
		return 0;

	/*
	 * Use an esr_context to build a fake header with a
	 * size greater then the free __reserved area minus HDR_SZ;
	 * using ESR_MAGIC here since it is not checked for size nor
	 * is limited to one instance.
	 *
	 * At first inject an additional normal esr_context
	 */
	head->magic = ESR_MAGIC;
	head->size = sizeof(struct esr_context);
	/* and terminate properly */
	write_terminator_record(GET_RESV_NEXT_HEAD(head));
	ASSERT_GOOD_CONTEXT(&sf.uc);

	/*
	 * now mess with fake esr_context size: leaving less space than
	 * needed while keeping size value 16-aligned
	 *
	 * It must trigger a SEGV from Kernel on:
	 *
	 *	resv_sz - offset < sizeof(*head)
	 */
	/* at first set the maximum good 16-aligned size */
	head->size = (resv_sz - offset - need_sz + MIN_SZ_ALIGN) & ~0xfUL;
	/* plus a bit more of 16-aligned sized stuff */
	head->size += MIN_SZ_ALIGN;
	/* and terminate properly */
	write_terminator_record(GET_RESV_NEXT_HEAD(head));
	ASSERT_BAD_CONTEXT(&sf.uc);
	fake_sigreturn(&sf, sizeof(sf), 0);

	return 1;
}

struct tdescr tde = {
		.name = "FAKE_SIGRETURN_BAD_SIZE",
		.descr = "Triggers a sigreturn with a overrun __reserved area",
		.sig_ok = SIGSEGV,
		.timeout = 3,
		.run = fake_sigreturn_bad_size_run,
};
