/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Mark Johnston <markj@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/mman.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

ATF_TC(brk_basic);
ATF_TC_HEAD(brk_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify basic brk() functionality");
}
ATF_TC_BODY(brk_basic, tc)
{
	void *oldbrk, *newbrk;
	int error;

	/* Reset the break. */
	error = brk(0);
	ATF_REQUIRE_MSG(error == 0, "brk: %s", strerror(errno));

	oldbrk = sbrk(0);
	ATF_REQUIRE(oldbrk != (void *)-1);

	/* Try to allocate a page. */
	error = brk((void *)((intptr_t)oldbrk + PAGE_SIZE * 2));
	ATF_REQUIRE_MSG(error == 0, "brk: %s", strerror(errno));

	/*
	 * Attempt to set the break below minbrk.  This should have no effect.
	 */
	error = brk((void *)((intptr_t)oldbrk - 1));
	ATF_REQUIRE_MSG(error == 0, "brk: %s", strerror(errno));
	newbrk = sbrk(0);
	ATF_REQUIRE_MSG(newbrk != (void *)-1, "sbrk: %s", strerror(errno));
	ATF_REQUIRE(newbrk == oldbrk);
}

ATF_TC(sbrk_basic);
ATF_TC_HEAD(sbrk_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Verify basic sbrk() functionality");
}
ATF_TC_BODY(sbrk_basic, tc)
{
	void *newbrk, *oldbrk;
	int *p;

	oldbrk = sbrk(0);
	ATF_REQUIRE_MSG(oldbrk != (void *)-1, "sbrk: %s", strerror(errno));
	p = sbrk(sizeof(*p));
	*p = 0;
	ATF_REQUIRE(oldbrk == p);

	newbrk = sbrk(-sizeof(*p));
	ATF_REQUIRE_MSG(newbrk != (void *)-1, "sbrk: %s", strerror(errno));
	ATF_REQUIRE(oldbrk == sbrk(0));

	oldbrk = sbrk(PAGE_SIZE * 2 + 1);
	ATF_REQUIRE_MSG(oldbrk != (void *)-1, "sbrk: %s", strerror(errno));
	memset(oldbrk, 0, PAGE_SIZE * 2 + 1);
	newbrk = sbrk(-(PAGE_SIZE * 2 + 1));
	ATF_REQUIRE_MSG(newbrk != (void *)-1, "sbrk: %s", strerror(errno));
	ATF_REQUIRE(sbrk(0) == oldbrk);
}

ATF_TC(mlockfuture);
ATF_TC_HEAD(mlockfuture, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that mlockall(MCL_FUTURE) applies to the data segment");
}
ATF_TC_BODY(mlockfuture, tc)
{
	void *oldbrk, *n, *newbrk;
	int error;
	char v;

	error = mlockall(MCL_FUTURE);
	ATF_REQUIRE_MSG(error == 0,
	    "mlockall: %s", strerror(errno));

	/*
	 * Advance the break so that at least one page is added to the data
	 * segment.  This page should be automatically faulted in to the address
	 * space.
	 */
	oldbrk = sbrk(0);
	ATF_REQUIRE(oldbrk != (void *)-1);
	newbrk = sbrk(PAGE_SIZE * 2);
	ATF_REQUIRE(newbrk != (void *)-1);

	n = (void *)(((uintptr_t)oldbrk + PAGE_SIZE) & ~PAGE_SIZE);
	v = 0;
	error = mincore(n, PAGE_SIZE, &v);
	ATF_REQUIRE_MSG(error == 0,
	    "mincore: %s", strerror(errno));
	ATF_REQUIRE_MSG((v & MINCORE_INCORE) != 0,
	    "unexpected page flags %#x", v);

	error = brk(oldbrk);
	ATF_REQUIRE(error == 0);

	error = munlockall();
	ATF_REQUIRE_MSG(error == 0,
	    "munlockall: %s", strerror(errno));
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, brk_basic);
	ATF_TP_ADD_TC(tp, sbrk_basic);
	ATF_TP_ADD_TC(tp, mlockfuture);

	return (atf_no_error());
}
