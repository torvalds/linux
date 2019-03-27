/*-
 * Copyright (c) 2003 Tim J. Robbins
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

/*
 * Test program for wctype() and iswctype() as specified by
 * IEEE Std. 1003.1-2001 and ISO/IEC 9899:1999.
 *
 * The functions are tested in the "C" and "ja_JP.eucJP" locales.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include <atf-c.h>

static void
require_lc_ctype(const char *locale_name)
{
	char *lc_ctype_set;

	lc_ctype_set = setlocale(LC_CTYPE, locale_name);
	if (lc_ctype_set == NULL)
		atf_tc_fail("setlocale(LC_CTYPE, \"%s\") failed; errno=%d",
		    locale_name, errno);

	ATF_REQUIRE(strcmp(lc_ctype_set, locale_name) == 0);
}

static wctype_t t;
static int i, j;
static struct {
	const char *name;
	int (*func)(wint_t);
} cls[] = {
	{ "alnum", iswalnum },
	{ "alpha", iswalpha },
	{ "blank", iswblank },
	{ "cntrl", iswcntrl },
	{ "digit", iswdigit },
	{ "graph", iswgraph },
	{ "lower", iswlower },
	{ "print", iswprint },
	{ "punct", iswpunct },
	{ "space", iswspace },
	{ "upper", iswupper },
	{ "xdigit", iswxdigit }
};

ATF_TC_WITHOUT_HEAD(iswctype_c_locale_test);
ATF_TC_BODY(iswctype_c_locale_test, tc)
{

	require_lc_ctype("C");
	for (i = 0; i < nitems(cls); i++) {
		t = wctype(cls[i].name);
		ATF_REQUIRE(t != 0);
		for (j = 0; j < 256; j++)
			ATF_REQUIRE(cls[i].func(j) == iswctype(j, t));
	}
	t = wctype("elephant");
	ATF_REQUIRE(t == 0);
	for (i = 0; i < 256; i++)
		ATF_REQUIRE(iswctype(i, t) == 0);
}

ATF_TC_WITHOUT_HEAD(iswctype_euc_jp_test);
ATF_TC_BODY(iswctype_euc_jp_test, tc)
{

	require_lc_ctype("ja_JP.eucJP");

	for (i = 0; i < nitems(cls); i++) {
		t = wctype(cls[i].name);
		ATF_REQUIRE(t != 0);
		for (j = 0; j < 65536; j++)
			ATF_REQUIRE(cls[i].func(j) == iswctype(j, t));
	}
	t = wctype("elephant");
	ATF_REQUIRE(t == 0);
	for (i = 0; i < 65536; i++)
		ATF_REQUIRE(iswctype(i, t) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, iswctype_c_locale_test);
	ATF_TP_ADD_TC(tp, iswctype_euc_jp_test);

	return (atf_no_error());
}
