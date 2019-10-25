/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 ARM Limited */

#ifndef __TEST_SIGNALS_UTILS_H__
#define __TEST_SIGNALS_UTILS_H__

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "test_signals.h"

int test_init(struct tdescr *td);
int test_setup(struct tdescr *td);
void test_cleanup(struct tdescr *td);
int test_run(struct tdescr *td);
void test_result(struct tdescr *td);

static inline bool feats_ok(struct tdescr *td)
{
	return (td->feats_required & td->feats_supported) == td->feats_required;
}

/*
 * Obtaining a valid and full-blown ucontext_t from userspace is tricky:
 * libc getcontext does() not save all the regs and messes with some of
 * them (pstate value in particular is not reliable).
 *
 * Here we use a service signal to grab the ucontext_t from inside a
 * dedicated signal handler, since there, it is populated by Kernel
 * itself in setup_sigframe(). The grabbed context is then stored and
 * made available in td->live_uc.
 *
 * As service-signal is used a SIGTRAP induced by a 'brk' instruction,
 * because here we have to avoid syscalls to trigger the signal since
 * they would cause any SVE sigframe content (if any) to be removed.
 *
 * Anyway this function really serves a dual purpose:
 *
 * 1. grab a valid sigcontext into td->live_uc for result analysis: in
 * such case it returns 1.
 *
 * 2. detect if, somehow, a previously grabbed live_uc context has been
 * used actively with a sigreturn: in such a case the execution would have
 * magically resumed in the middle of this function itself (seen_already==1):
 * in such a case return 0, since in fact we have not just simply grabbed
 * the context.
 *
 * This latter case is useful to detect when a fake_sigreturn test-case has
 * unexpectedly survived without hitting a SEGV.
 *
 * Note that the case of runtime dynamically sized sigframes (like in SVE
 * context) is still NOT addressed: sigframe size is supposed to be fixed
 * at sizeof(ucontext_t).
 */
static __always_inline bool get_current_context(struct tdescr *td,
						ucontext_t *dest_uc)
{
	static volatile bool seen_already;

	assert(td && dest_uc);
	/* it's a genuine invocation..reinit */
	seen_already = 0;
	td->live_uc_valid = 0;
	td->live_sz = sizeof(*dest_uc);
	memset(dest_uc, 0x00, td->live_sz);
	td->live_uc = dest_uc;
	/*
	 * Grab ucontext_t triggering a SIGTRAP.
	 *
	 * Note that:
	 * - live_uc_valid is declared volatile sig_atomic_t in
	 *   struct tdescr since it will be changed inside the
	 *   sig_copyctx handler
	 * - the additional 'memory' clobber is there to avoid possible
	 *   compiler's assumption on live_uc_valid and the content
	 *   pointed by dest_uc, which are all changed inside the signal
	 *   handler
	 * - BRK causes a debug exception which is handled by the Kernel
	 *   and finally causes the SIGTRAP signal to be delivered to this
	 *   test thread. Since such delivery happens on the ret_to_user()
	 *   /do_notify_resume() debug exception return-path, we are sure
	 *   that the registered SIGTRAP handler has been run to completion
	 *   before the execution path is restored here: as a consequence
	 *   we can be sure that the volatile sig_atomic_t live_uc_valid
	 *   carries a meaningful result. Being in a single thread context
	 *   we'll also be sure that any access to memory modified by the
	 *   handler (namely ucontext_t) will be visible once returned.
	 * - note that since we are using a breakpoint instruction here
	 *   to cause a SIGTRAP, the ucontext_t grabbed from the signal
	 *   handler would naturally contain a PC pointing exactly to this
	 *   BRK line, which means that, on return from the signal handler,
	 *   or if we place the ucontext_t on the stack to fake a sigreturn,
	 *   we'll end up in an infinite loop of BRK-SIGTRAP-handler.
	 *   For this reason we take care to artificially move forward the
	 *   PC to the next instruction while inside the signal handler.
	 */
	asm volatile ("brk #666"
		      : "+m" (*dest_uc)
		      :
		      : "memory");

	/*
	 * If we get here with seen_already==1 it implies the td->live_uc
	 * context has been used to get back here....this probably means
	 * a test has failed to cause a SEGV...anyway live_uc does not
	 * point to a just acquired copy of ucontext_t...so return 0
	 */
	if (seen_already) {
		fprintf(stdout,
			"Unexpected successful sigreturn detected: live_uc is stale !\n");
		return 0;
	}
	seen_already = 1;

	return td->live_uc_valid;
}

int fake_sigreturn(void *sigframe, size_t sz, int misalign_bytes);
#endif
