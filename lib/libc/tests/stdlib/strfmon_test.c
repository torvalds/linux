/*-
 * Copyright (C) 2018 Conrad Meyer <cem@FreeBSD.org>
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

#include <locale.h>
#include <monetary.h>
#include <stdio.h>

#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(strfmon_locale_thousands);
ATF_TC_BODY(strfmon_locale_thousands, tc)
{
	char actual[40], expected[40];
	struct lconv *lc;
	const char *ts;
	double n;

	setlocale(LC_MONETARY, "sv_SE.UTF-8");

	lc = localeconv();

	ts = lc->mon_thousands_sep;
	if (strlen(ts) == 0)
		ts = lc->thousands_sep;

	if (strlen(ts) < 2)
		atf_tc_skip("multi-byte thousands-separator not found");

	n = 1234.56;
	strfmon(actual, sizeof(actual), "%i", n);

	strcpy(expected, "1");
	strlcat(expected, ts, sizeof(expected));
	strlcat(expected, "234", sizeof(expected));

	/* We're just testing the thousands separator, not all of strmon. */
	actual[strlen(expected)] = '\0';
	ATF_CHECK_STREQ(expected, actual);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, strfmon_locale_thousands);
	return (atf_no_error());
}
