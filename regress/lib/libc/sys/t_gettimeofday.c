/*	$OpenBSD: t_gettimeofday.c,v 1.5 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_gettimeofday.c,v 1.1 2011/07/07 06:57:53 jruoho Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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

#include <sys/time.h>

#include "atf-c.h"
#include <errno.h>
#include <signal.h>
#include <string.h>

#ifdef __OpenBSD__
static void	sighandler(int);

static void
sighandler(int signo)
{
	_exit(0);
}
#endif

ATF_TC(gettimeofday_err);
ATF_TC_HEAD(gettimeofday_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from gettimeofday(2)");
}

ATF_TC_BODY(gettimeofday_err, tc)
{

#ifdef __OpenBSD__
	/*
	 * With userland timecounters we will generate SIGSEGV instead
	 * of failing with errno set to EFAULT.  POSIX explicitly
	 * allows this behaviour.
	 */
	ATF_REQUIRE(signal(SIGSEGV, sighandler) != SIG_ERR);
	/* On sparc64 dereferencing -1 causes SIGBUS */
	ATF_REQUIRE(signal(SIGBUS, sighandler) != SIG_ERR);
#endif
	errno = 0;

	ATF_REQUIRE_ERRNO(EFAULT, gettimeofday((void *)-1, NULL) != 0);
}

ATF_TC(gettimeofday_mono);
ATF_TC_HEAD(gettimeofday_mono, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test monotonicity of gettimeofday(2)");
}

ATF_TC_BODY(gettimeofday_mono, tc)
{
	static const size_t maxiter = 100;
	struct timeval tv1, tv2;
	size_t i;

	for (i = 0; i < maxiter; i++) {

		(void)memset(&tv1, 0, sizeof(struct timeval));
		(void)memset(&tv2, 0, sizeof(struct timeval));

		ATF_REQUIRE(gettimeofday(&tv1, NULL) == 0);
		ATF_REQUIRE(gettimeofday(&tv2, NULL) == 0);

		if (timercmp(&tv2, &tv1, <) != 0)
			atf_tc_fail("time went backwards");
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, gettimeofday_err);
	ATF_TP_ADD_TC(tp, gettimeofday_mono);

	return atf_no_error();
}
