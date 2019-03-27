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
 * Test program for mbrtowc(), as specified by IEEE Std. 1003.1-2001 and
 * ISO/IEC 9899:1999.
 *
 * The function is tested with both the "C" ("POSIX") LC_CTYPE setting and
 * "ja_JP.eucJP". Other encodings are not tested.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(mbrtowc_test);
ATF_TC_BODY(mbrtowc_test, tc)
{
	mbstate_t s;
	wchar_t wc;
	char buf[MB_LEN_MAX + 1];

	/*
	 * C/POSIX locale.
	 */

	ATF_REQUIRE(MB_CUR_MAX == 1);

	/* Null wide character, internal state. */
	memset(buf, 0xcc, sizeof(buf));
	buf[0] = 0;
	ATF_REQUIRE(mbrtowc(&wc, buf, 1, NULL) == 0);
	ATF_REQUIRE(wc == 0);

	/* Null wide character. */
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtowc(&wc, buf, 1, &s) == 0);
	ATF_REQUIRE(wc == 0);

	/* Latin letter A, internal state. */
	ATF_REQUIRE(mbrtowc(NULL, 0, 0, NULL) == 0);
	buf[0] = 'A';
	ATF_REQUIRE(mbrtowc(&wc, buf, 1, NULL) == 1);
	ATF_REQUIRE(wc == L'A');

	/* Latin letter A. */
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtowc(&wc, buf, 1, &s) == 1);
	ATF_REQUIRE(wc == L'A');

	/* Incomplete character sequence. */
	wc = L'z';
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtowc(&wc, buf, 0, &s) == (size_t)-2);
	ATF_REQUIRE(wc == L'z');

	/* Check that mbrtowc() doesn't access the buffer when n == 0. */
	wc = L'z';
	memset(&s, 0, sizeof(s));
	buf[0] = '\0';
	ATF_REQUIRE(mbrtowc(&wc, buf, 0, &s) == (size_t)-2);
	ATF_REQUIRE(wc == L'z');

	/*
	 * Japanese (EUC) locale.
	 */

	ATF_REQUIRE(strcmp(setlocale(LC_CTYPE, "ja_JP.eucJP"), "ja_JP.eucJP") == 0);
	ATF_REQUIRE(MB_CUR_MAX > 1);

	/* Null wide character, internal state. */
	ATF_REQUIRE(mbrtowc(NULL, 0, 0, NULL) == 0);
	memset(buf, 0xcc, sizeof(buf));
	buf[0] = 0;
	ATF_REQUIRE(mbrtowc(&wc, buf, 1, NULL) == 0);
	ATF_REQUIRE(wc == 0);

	/* Null wide character. */
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtowc(&wc, buf, 1, &s) == 0);
	ATF_REQUIRE(wc == 0);

	/* Latin letter A, internal state. */
	ATF_REQUIRE(mbrtowc(NULL, 0, 0, NULL) == 0);
	buf[0] = 'A';
	ATF_REQUIRE(mbrtowc(&wc, buf, 1, NULL) == 1);
	ATF_REQUIRE(wc == L'A');

	/* Latin letter A. */
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtowc(&wc, buf, 1, &s) == 1);
	ATF_REQUIRE(wc == L'A');

	/* Incomplete character sequence (zero length). */
	wc = L'z';
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtowc(&wc, buf, 0, &s) == (size_t)-2);
	ATF_REQUIRE(wc == L'z');

	/* Incomplete character sequence (truncated double-byte). */
	memset(buf, 0xcc, sizeof(buf));
	buf[0] = 0xa3;
	buf[1] = 0x00;
	memset(&s, 0, sizeof(s));
	wc = 0;
	ATF_REQUIRE(mbrtowc(&wc, buf, 1, &s) == (size_t)-2);

	/* Same as above, but complete. */
	buf[1] = 0xc1;
	memset(&s, 0, sizeof(s));
	wc = 0;
	ATF_REQUIRE(mbrtowc(&wc, buf, 2, &s) == 2);
	ATF_REQUIRE(wc == 0xa3c1);

	/* Test restarting behaviour. */
	memset(buf, 0xcc, sizeof(buf));
	buf[0] = 0xa3;
	memset(&s, 0, sizeof(s));
	wc = 0;
	ATF_REQUIRE(mbrtowc(&wc, buf, 1, &s) == (size_t)-2);
	ATF_REQUIRE(wc == 0);
	buf[0] = 0xc1;
	ATF_REQUIRE(mbrtowc(&wc, buf, 1, &s) == 1);
	ATF_REQUIRE(wc == 0xa3c1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mbrtowc_test);

	return (atf_no_error());
}
