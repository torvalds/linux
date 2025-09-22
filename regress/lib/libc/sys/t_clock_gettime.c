/*	$OpenBSD: t_clock_gettime.c,v 1.3 2021/12/13 16:56:48 deraadt Exp $	*/
/* $NetBSD: t_clock_gettime.c,v 1.3 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Frank Kardel.
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

/*-
 * Copyright (c) 2006 Frank Kardel
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

#include <sys/sysctl.h>


#include "atf-c.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "h_macros.h"

#define MINPOSDIFF	15000000	/* 15 ms for now */
#define TIMEOUT		5

#define TC_HARDWARE	"kern.timecounter.hardware"
#define TC_CHOICE	"kern.timecounter.choice"

static void
check_timecounter(void)
{
	struct timespec tsa, tsb, tsl, res;
	long long mindiff = INTMAX_MAX;
	time_t endlimit;

#define CL(x) \
	do { \
		if ((x) != -1) \
			break; \
		atf_tc_fail_nonfatal("%s: %s", #x, strerror(errno)); \
		return; \
	} while (0)

	CL(clock_gettime(CLOCK_REALTIME, &tsa));
	tsl = tsa;

	CL(time(&endlimit));
	endlimit += TIMEOUT + 1;

	while ((time_t)tsa.tv_sec < endlimit) {
		long long diff;

		CL(clock_gettime(CLOCK_REALTIME, &tsb));
		diff = 1000000000LL * (tsb.tv_sec - tsa.tv_sec)
		    + tsb.tv_nsec - tsa.tv_nsec;

		if (diff > 0 && mindiff > diff)
			mindiff = diff;

		if (diff < 0 || diff > MINPOSDIFF) {
			long long elapsed;
			(void)printf("%stime TSA: 0x%jx.%08jx, TSB: 0x%jx.%08jx, "
			    "diff = %lld nsec, ", (diff < 0) ? "BAD " : "",
			    (uintmax_t)tsa.tv_sec, (uintmax_t)tsa.tv_nsec,
			    (uintmax_t)tsb.tv_sec, (uintmax_t)tsb.tv_nsec, diff);

			elapsed = 1000000000LL * (tsb.tv_sec - tsl.tv_sec)
			    + tsb.tv_nsec - tsl.tv_nsec;


			(void)printf("%lld nsec\n", elapsed);
			tsl = tsb;

			ATF_CHECK(diff >= 0);
			if (diff < 0)
				return;
		}

		tsa.tv_sec = tsb.tv_sec;
		tsa.tv_nsec = tsb.tv_nsec;
	}

	if (clock_getres(CLOCK_REALTIME, &res) == 0) {
		long long r = res.tv_sec * 1000000000 + res.tv_nsec;

		(void)printf("Claimed resolution: %lld nsec (%f Hz) or "
		    "better\n", r, 1.0 / r * 1e9);
		(void)printf("Observed minimum non zero delta: %lld "
		    "nsec\n", mindiff);
	}

#undef CL
}

ATF_TC(clock_gettime_real);
ATF_TC_HEAD(clock_gettime_real, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
	atf_tc_set_md_var(tc, "descr",
	    "Checks the monotonicity of the CLOCK_REALTIME implementation");
	atf_tc_set_md_var(tc, "timeout", "300");
}

ATF_TC_BODY(clock_gettime_real, tc)
{
	char name[128], cbuf[512], ctrbuf[10240];
	size_t cbufsiz = sizeof(cbuf);
	size_t ctrbufsiz = sizeof(ctrbuf);
	const char *p;
	char *save;
	int quality, n;

	if (sysctlbyname(TC_HARDWARE, cbuf, &cbufsiz, NULL, 0) != 0) {
		(void)printf("\nChecking legacy time implementation "
		    "for %d seconds\n", TIMEOUT);
		check_timecounter();
		return;
		/* NOTREACHED */
	}
	(void)printf("%s = %s\n", TC_HARDWARE, cbuf);
	REQUIRE_LIBC(save = strdup(cbuf), NULL);

	RL(sysctlbyname(TC_CHOICE, ctrbuf, &ctrbufsiz, NULL, 0));
	(void)printf("%s = %s\n", TC_CHOICE, ctrbuf);

	for (p = ctrbuf, n = 0; sscanf(p, "%127[^(](q=%d, f=%*u Hz)%*[ ]%n",
	    name, &quality, &n) == 2; p += n) {
		struct timespec ts;
		int ret;

		if (quality < 0)
			continue;

		(void)printf("\nChecking %s for %d seconds\n", name, TIMEOUT);
		CHECK_LIBC(ret = sysctlbyname(TC_HARDWARE, NULL, 0,
		    name, strlen(name)), -1);
		if (ret == -1)
			continue;

		/* wait a bit to select new counter in clockinterrupt */
		ts.tv_sec = 0;
		ts.tv_nsec = 100000000;
		(void)nanosleep(&ts, NULL);

		check_timecounter();
	}

	RL(sysctlbyname(TC_HARDWARE, NULL, 0, save, strlen(save)));
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, clock_gettime_real);

	return atf_no_error();
}
