/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018 Eric van Gyzen
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <atf-c.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>

/*
 * TODO add tests for:
 *
 * - all error cases
 * - SS_DISABLE
 * - no effect without SA_ONSTACK
 */

static sig_atomic_t disabled_correct = 1;
static sig_atomic_t level1_correct = -1;
static sig_atomic_t level2_correct = -1;

static void
sig_handler(int signo, siginfo_t *info __unused, void *ucp)
{
	ucontext_t *uc = ucp;

	// The alternate signal stack is enabled.
	disabled_correct = disabled_correct &&
	    (uc->uc_stack.ss_flags & SS_DISABLE) == 0;
	if (signo == SIGUSR1) {
		// The thread was NOT running on the alternate signal
		// stack when this signal arrived.
		level1_correct = (uc->uc_stack.ss_flags & SS_ONSTACK) == 0;
		raise(SIGUSR2);
	} else {
		// The thread WAS running on the alternate signal
		// stack when this signal arrived.
		level2_correct = (uc->uc_stack.ss_flags & SS_ONSTACK) != 0;
	}
}

ATF_TC(ss_onstack);

ATF_TC_HEAD(ss_onstack, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test reporting of SS_ONSTACK");
}

ATF_TC_BODY(ss_onstack, tc)
{
	stack_t ss = {
		.ss_size = SIGSTKSZ,
	};
	stack_t oss = {
		.ss_size = 0,
	};

	ss.ss_sp = malloc(ss.ss_size);
	ATF_REQUIRE(ss.ss_sp != NULL);
	ATF_REQUIRE(sigaltstack(&ss, &oss) == 0);

	// There should be no signal stack currently configured.
	ATF_CHECK(oss.ss_sp == NULL);
	ATF_CHECK(oss.ss_size == 0);
	ATF_CHECK((oss.ss_flags & SS_DISABLE) != 0);
	ATF_CHECK((oss.ss_flags & SS_ONSTACK) == 0);

	struct sigaction sa = {
		.sa_sigaction = sig_handler,
		.sa_flags = SA_ONSTACK | SA_SIGINFO,
	};
	ATF_REQUIRE(sigemptyset(&sa.sa_mask) == 0);
	ATF_REQUIRE(sigaction(SIGUSR1, &sa, NULL) == 0);
	ATF_REQUIRE(sigaction(SIGUSR2, &sa, NULL) == 0);
	ATF_REQUIRE(raise(SIGUSR1) == 0);

	ATF_CHECK(level1_correct != -1);
	ATF_CHECK(level1_correct == 1);
	ATF_CHECK(level2_correct != -1);
	ATF_CHECK(level2_correct == 1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, ss_onstack);

	return (atf_no_error());
}
