/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 John H. Baldwin <jhb@FreeBSD.org>
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
#include <ucontext.h>

static char uc_stack[16 * 1024];

static void
check_1(int arg1)
{

	ATF_REQUIRE_EQ(arg1, 1);
}

ATF_TC_WITHOUT_HEAD(makecontext_arg1);
ATF_TC_BODY(makecontext_arg1, tc)
{
	ucontext_t ctx[2];

	ATF_REQUIRE_EQ(getcontext(&ctx[1]), 0);
	ctx[1].uc_stack.ss_sp = uc_stack;
	ctx[1].uc_stack.ss_size = sizeof(uc_stack);
	ctx[1].uc_link = &ctx[0];
	makecontext(&ctx[1], (void (*)(void))check_1, 1, 1);

	ATF_REQUIRE_EQ(swapcontext(&ctx[0], &ctx[1]), 0);
}

static void
check_2(int arg1, int arg2)
{

	ATF_REQUIRE_EQ(arg1, 1);
	ATF_REQUIRE_EQ(arg2, 2);
}

ATF_TC_WITHOUT_HEAD(makecontext_arg2);
ATF_TC_BODY(makecontext_arg2, tc)
{
	ucontext_t ctx[2];

	ATF_REQUIRE_EQ(getcontext(&ctx[1]), 0);
	ctx[1].uc_stack.ss_sp = uc_stack;
	ctx[1].uc_stack.ss_size = sizeof(uc_stack);
	ctx[1].uc_link = &ctx[0];
	makecontext(&ctx[1], (void (*)(void))check_2, 2, 1, 2);

	ATF_REQUIRE_EQ(swapcontext(&ctx[0], &ctx[1]), 0);
}

static void
check_3(int arg1, int arg2, int arg3)
{

	ATF_REQUIRE_EQ(arg1, 1);
	ATF_REQUIRE_EQ(arg2, 2);
	ATF_REQUIRE_EQ(arg3, 3);
}

ATF_TC_WITHOUT_HEAD(makecontext_arg3);
ATF_TC_BODY(makecontext_arg3, tc)
{
	ucontext_t ctx[2];

	ATF_REQUIRE_EQ(getcontext(&ctx[1]), 0);
	ctx[1].uc_stack.ss_sp = uc_stack;
	ctx[1].uc_stack.ss_size = sizeof(uc_stack);
	ctx[1].uc_link = &ctx[0];
	makecontext(&ctx[1], (void (*)(void))check_3, 3, 1, 2, 3);

	ATF_REQUIRE_EQ(swapcontext(&ctx[0], &ctx[1]), 0);
}

static void
check_4(int arg1, int arg2, int arg3, int arg4)
{

	ATF_REQUIRE_EQ(arg1, 1);
	ATF_REQUIRE_EQ(arg2, 2);
	ATF_REQUIRE_EQ(arg3, 3);
	ATF_REQUIRE_EQ(arg4, 4);
}

ATF_TC_WITHOUT_HEAD(makecontext_arg4);
ATF_TC_BODY(makecontext_arg4, tc)
{
	ucontext_t ctx[2];

	ATF_REQUIRE_EQ(getcontext(&ctx[1]), 0);
	ctx[1].uc_stack.ss_sp = uc_stack;
	ctx[1].uc_stack.ss_size = sizeof(uc_stack);
	ctx[1].uc_link = &ctx[0];
	makecontext(&ctx[1], (void (*)(void))check_4, 4, 1, 2, 3, 4);

	ATF_REQUIRE_EQ(swapcontext(&ctx[0], &ctx[1]), 0);
}

static void
check_5(int arg1, int arg2, int arg3, int arg4, int arg5)
{

	ATF_REQUIRE_EQ(arg1, 1);
	ATF_REQUIRE_EQ(arg2, 2);
	ATF_REQUIRE_EQ(arg3, 3);
	ATF_REQUIRE_EQ(arg4, 4);
	ATF_REQUIRE_EQ(arg5, 5);
}

ATF_TC_WITHOUT_HEAD(makecontext_arg5);
ATF_TC_BODY(makecontext_arg5, tc)
{
	ucontext_t ctx[2];

	ATF_REQUIRE_EQ(getcontext(&ctx[1]), 0);
	ctx[1].uc_stack.ss_sp = uc_stack;
	ctx[1].uc_stack.ss_size = sizeof(uc_stack);
	ctx[1].uc_link = &ctx[0];
	makecontext(&ctx[1], (void (*)(void))check_5, 5, 1, 2, 3, 4, 5);

	ATF_REQUIRE_EQ(swapcontext(&ctx[0], &ctx[1]), 0);
}

static void
check_6(int arg1, int arg2, int arg3, int arg4, int arg5, int arg6)
{

	ATF_REQUIRE_EQ(arg1, 1);
	ATF_REQUIRE_EQ(arg2, 2);
	ATF_REQUIRE_EQ(arg3, 3);
	ATF_REQUIRE_EQ(arg4, 4);
	ATF_REQUIRE_EQ(arg5, 5);
	ATF_REQUIRE_EQ(arg6, 6);
}

ATF_TC_WITHOUT_HEAD(makecontext_arg6);
ATF_TC_BODY(makecontext_arg6, tc)
{
	ucontext_t ctx[2];

	ATF_REQUIRE_EQ(getcontext(&ctx[1]), 0);
	ctx[1].uc_stack.ss_sp = uc_stack;
	ctx[1].uc_stack.ss_size = sizeof(uc_stack);
	ctx[1].uc_link = &ctx[0];
	makecontext(&ctx[1], (void (*)(void))check_6, 6, 1, 2, 3, 4, 5, 6);

	ATF_REQUIRE_EQ(swapcontext(&ctx[0], &ctx[1]), 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, makecontext_arg1);
	ATF_TP_ADD_TC(tp, makecontext_arg2);
	ATF_TP_ADD_TC(tp, makecontext_arg3);
	ATF_TP_ADD_TC(tp, makecontext_arg4);
	ATF_TP_ADD_TC(tp, makecontext_arg5);
	ATF_TP_ADD_TC(tp, makecontext_arg6);

	return (atf_no_error());
}
