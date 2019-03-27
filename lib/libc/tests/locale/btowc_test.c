/*-
 * Copyright (c) 2002 Tim J. Robbins
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
 * Test program for btowc() and wctob() as specified by IEEE Std. 1003.1-2001
 * and ISO/IEC 9899:1999.
 *
 * The function is tested in the "C" and "ja_JP.eucJP" locales.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(btowc_test);
ATF_TC_BODY(btowc_test, tc)
{
	int i;

	/* C/POSIX locale. */
	ATF_REQUIRE(btowc(EOF) == WEOF);
	ATF_REQUIRE(wctob(WEOF) == EOF);
	for (i = 0; i < UCHAR_MAX; i++)
		ATF_REQUIRE(btowc(i) == (wchar_t)i && i == (int)wctob(i));

	/* Japanese (EUC) locale. */
	ATF_REQUIRE(strcmp(setlocale(LC_CTYPE, "ja_JP.eucJP"), "ja_JP.eucJP") == 0);
	ATF_REQUIRE(MB_CUR_MAX > 1);
	ATF_REQUIRE(btowc('A') == L'A' && wctob(L'A') == 'A');
	ATF_REQUIRE(btowc(0xa3) == WEOF && wctob(0xa3c1) == EOF);

}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, btowc_test);

	return (atf_no_error());
}
