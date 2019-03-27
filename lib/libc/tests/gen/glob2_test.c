/*
 * Copyright (c) 2017 Dell EMC Isilon
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
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <atf-c.h>

/*
 * Derived from Russ Cox' pathological case test program used for the
 * https://research.swtch.com/glob article.
 */
ATF_TC_WITHOUT_HEAD(glob_pathological_test);
ATF_TC_BODY(glob_pathological_test, tc)
{
	struct timespec t, t2;
	glob_t g;
	const char *longname = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	char pattern[1000], *p;
	double dt;
	unsigned i, j, k, mul;
	int fd, rc;

	fd = open(longname, O_CREAT | O_RDWR, 0666);
	ATF_REQUIRE(fd >= 0);

	/*
	 * Test up to 100 a* groups.  Exponential implementations typically go
	 * bang at i=7 or 8.
	 */
	for (i = 0; i < 100; i++) {
		/*
		 * Create a*...b pattern with i 'a*' groups.
		 */
		p = pattern;
		for (k = 0; k < i; k++) {
			*p++ = 'a';
			*p++ = '*';
		}
		*p++ = 'b';
		*p = '\0';

		clock_gettime(CLOCK_REALTIME, &t);
		for (j = 0; j < mul; j++) {
			memset(&g, 0, sizeof g);
			rc = glob(pattern, 0, 0, &g);
			if (rc == GLOB_NOSPACE || rc == GLOB_ABORTED) {
				ATF_REQUIRE_MSG(rc == GLOB_NOMATCH,
				    "an unexpected error occurred: "
				    "rc=%d errno=%d", rc, errno);
				/* NORETURN */
			}

			ATF_CHECK_MSG(rc == GLOB_NOMATCH,
			    "A bogus match occurred: '%s' ~ '%s'", pattern,
			    g.gl_pathv[0]);
			globfree(&g);
		}
		clock_gettime(CLOCK_REALTIME, &t2);

		t2.tv_sec -= t.tv_sec;
		t2.tv_nsec -= t.tv_nsec;
		dt = t2.tv_sec + (double)t2.tv_nsec/1e9;
		dt /= mul;

		ATF_CHECK_MSG(dt < 1, "glob(3) took far too long: %d %.9f", i,
		    dt);

		if (dt >= 0.0001)
			mul = 1;
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, glob_pathological_test);

	return (atf_no_error());
}
