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
 * Test program for wctrans() and towctrans() as specified by
 * IEEE Std. 1003.1-2001 and ISO/IEC 9899:1999.
 *
 * The functions are tested in the "C" and "ja_JP.eucJP" locales.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(towctrans_test);
ATF_TC_BODY(towctrans_test, tc)
{
	wctype_t t;
	int i, j;
	struct {
		const char *name;
		wint_t (*func)(wint_t);
	} tran[] = {
		{ "tolower", towlower },
		{ "toupper", towupper },
	};

	/* C/POSIX locale. */
	for (i = 0; i < sizeof(tran) / sizeof(*tran); i++) {
		t = wctrans(tran[i].name);
		ATF_REQUIRE(t != 0);
		for (j = 0; j < 256; j++)
			ATF_REQUIRE(tran[i].func(j) == towctrans(j, t));
	}
	t = wctrans("elephant");
	ATF_REQUIRE(t == 0);
	for (i = 0; i < 256; i++)
		ATF_REQUIRE(towctrans(i, t) == i);

	/* Japanese (EUC) locale. */
	ATF_REQUIRE(strcmp(setlocale(LC_CTYPE, "ja_JP.eucJP"), "ja_JP.eucJP") == 0);
	for (i = 0; i < sizeof(tran) / sizeof(*tran); i++) {
		t = wctrans(tran[i].name);
		ATF_REQUIRE(t != 0);
		for (j = 0; j < 65536; j++)
			ATF_REQUIRE(tran[i].func(j) == towctrans(j, t));
	}
	t = wctrans("elephant");
	ATF_REQUIRE(t == 0);
	for (i = 0; i < 65536; i++)
		ATF_REQUIRE(towctrans(i, t) == i);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, towctrans_test);

	return (atf_no_error());
}
