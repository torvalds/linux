/*	$OpenBSD: t_sigaltstack.c,v 1.2 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_sigaltstack.c,v 1.2 2020/05/01 21:35:30 christos Exp $ */

/*-
 * Copyright (c) 2020 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "macros.h"

#include <signal.h>
#include <stdbool.h>

#include "atf-c.h"

#include "h_macros.h"

static stack_t sigstk;
static bool handler_called;
static bool handler_use_altstack;

static void
handler(int signo __unused)
{
	char sp[128];

	handler_called = true;

	/* checking if the stack pointer is within the range of altstack */
	if ((char *)sigstk.ss_sp <= sp &&
	    ((char *)sigstk.ss_sp + sigstk.ss_size) > sp)
		handler_use_altstack = true;
	else
		handler_use_altstack = false;
}

ATF_TC(sigaltstack_onstack);
ATF_TC_HEAD(sigaltstack_onstack, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks for using signal stack with SA_ONSTACK");
}

ATF_TC_BODY(sigaltstack_onstack, tc)
{
	struct sigaction sa;
	int i;

	/* set a signal handler use alternative stack */
	memset(&sigstk, 0, sizeof(sigstk));
	sigstk.ss_sp = malloc(SIGSTKSZ);
	ATF_REQUIRE(sigstk.ss_sp != NULL);
	sigstk.ss_size = SIGSTKSZ;
	sigstk.ss_flags = 0;
	ATF_REQUIRE(sigaltstack(&sigstk, 0) == 0);

	sigemptyset(&sa.sa_mask);
	sa.sa_handler = handler;
	sa.sa_flags = SA_ONSTACK;
	sigaction(SIGUSR1, &sa, NULL);

	/* test several times */
	for (i = 1; i <= 5; i++) {
		handler_called = false;
		kill(getpid(), SIGUSR1);

		if (!handler_called)
			atf_tc_fail("signal handler wasn't called (count=%d)", i);
		if (!handler_use_altstack)
			atf_tc_fail("alternative stack wasn't used (count=%d)", i);
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, sigaltstack_onstack);

	return atf_no_error();
}
