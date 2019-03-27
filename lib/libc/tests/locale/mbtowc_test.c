/*-
 * Copyright (c) 2002-2004 Tim J. Robbins
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
 * Test program for mbtowc(), as specified by IEEE Std. 1003.1-2001 and
 * ISO/IEC 9899:1990.
 *
 * The function is tested with both the "C" ("POSIX") LC_CTYPE setting and
 * "ja_JP.eucJP". Other encodings are not tested.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(mbtowc_test);
ATF_TC_BODY(mbtowc_test, tc)
{
	char buf[MB_LEN_MAX + 1];
	wchar_t wc;

	/* C/POSIX locale. */

	ATF_REQUIRE(MB_CUR_MAX == 1);

	/* No shift states in C locale. */
	ATF_REQUIRE(mbtowc(NULL, NULL, 0) == 0);

	/* Null wide character. */
	wc = 0xcccc;
	memset(buf, 0, sizeof(buf));
	ATF_REQUIRE(mbtowc(&wc, buf, 1) == 0);
	ATF_REQUIRE(wc == 0);

	/* Latin letter A. */
	buf[0] = 'A';
	ATF_REQUIRE(mbtowc(&wc, buf, 1) == 1);
	ATF_REQUIRE(wc == L'A');

	/* Incomplete character sequence. */
	wc = L'z';
	buf[0] = '\0';
	ATF_REQUIRE(mbtowc(&wc, buf, 0) == -1);
	ATF_REQUIRE(wc == L'z');
	ATF_REQUIRE(mbtowc(NULL, NULL, 0) == 0);

	/* Japanese (EUC) locale. */

	ATF_REQUIRE(strcmp(setlocale(LC_CTYPE, "ja_JP.eucJP"), "ja_JP.eucJP") == 0);
	ATF_REQUIRE(MB_CUR_MAX > 1);

	/* Null wide character */
	memset(buf, 0xcc, sizeof(buf));
	buf[0] = 0;
	wc = 0xcccc;
	ATF_REQUIRE(mbtowc(&wc, buf, 1) == 0);
	ATF_REQUIRE(wc == 0);

	/* Latin letter A. */
	buf[0] = 'A';
	ATF_REQUIRE(mbtowc(&wc, buf, 1) == 1);
	ATF_REQUIRE(wc == L'A');

	/* Incomplete character sequence (zero length). */
	wc = L'z';
	buf[0] = '\0';
	ATF_REQUIRE(mbtowc(&wc, buf, 0) == -1);
	ATF_REQUIRE(wc == L'z');
	ATF_REQUIRE(mbtowc(NULL, NULL, 0) == 0);

	/* Incomplete character sequence (truncated double-byte). */
	memset(buf, 0xcc, sizeof(buf));
	buf[0] = 0xa3;
	buf[1] = 0x00;
	wc = L'z';
	ATF_REQUIRE(mbtowc(&wc, buf, 1) == -1);
	ATF_REQUIRE(wc == L'z');
	ATF_REQUIRE(mbtowc(NULL, NULL, 0) == 0);

	/* Same as above, but complete. */
	buf[1] = 0xc1;
	ATF_REQUIRE(mbtowc(&wc, buf, 2) == 2);
	ATF_REQUIRE(wc == 0xa3c1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mbtowc_test);

	return (atf_no_error());
}
