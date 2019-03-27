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
 * Test program for wcsrtombs(), as specified by IEEE Std. 1003.1-2001 and
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

ATF_TC_WITHOUT_HEAD(wcsrtombs_test);
ATF_TC_BODY(wcsrtombs_test, tc)
{
	wchar_t srcbuf[128];
	char dstbuf[128];
	wchar_t *src;
	mbstate_t s;

	/* C/POSIX locale. */

	/* Simple null terminated string. */
	wmemset(srcbuf, 0xcc, sizeof(srcbuf) / sizeof(*srcbuf));
	wcscpy(srcbuf, L"hello");
	memset(dstbuf, 0xcc, sizeof(dstbuf));
	src = srcbuf;
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(wcsrtombs(dstbuf, (const wchar_t **)&src, sizeof(dstbuf),
	    &s) == 5);
	ATF_REQUIRE(strcmp(dstbuf, "hello") == 0);
	ATF_REQUIRE((unsigned char)dstbuf[6] == 0xcc);
	ATF_REQUIRE(src == NULL);

	/* Not enough space in destination buffer. */
	wmemset(srcbuf, 0xcc, sizeof(srcbuf) / sizeof(*srcbuf));
	wcscpy(srcbuf, L"hello");
	memset(dstbuf, 0xcc, sizeof(dstbuf));
	src = srcbuf;
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(wcsrtombs(dstbuf, (const wchar_t **)&src, 4,
	    &s) == 4);
	ATF_REQUIRE(memcmp(dstbuf, "hell", 4) == 0);
	ATF_REQUIRE((unsigned char)dstbuf[5] == 0xcc);
	ATF_REQUIRE(src == srcbuf + 4);

	/* Null terminated string, internal dest. buffer */
	wmemset(srcbuf, 0xcc, sizeof(srcbuf) / sizeof(*srcbuf));
	wcscpy(srcbuf, L"hello");
	src = srcbuf;
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(wcsrtombs(NULL, (const wchar_t **)&src, sizeof(dstbuf),
	    &s) == 5);

	/* Null terminated string, internal state. */
	wmemset(srcbuf, 0xcc, sizeof(srcbuf) / sizeof(*srcbuf));
	wcscpy(srcbuf, L"hello");
	memset(dstbuf, 0xcc, sizeof(dstbuf));
	src = srcbuf;
	ATF_REQUIRE(wcsrtombs(dstbuf, (const wchar_t **)&src, sizeof(dstbuf),
	    NULL) == 5);
	ATF_REQUIRE(strcmp(dstbuf, "hello") == 0);
	ATF_REQUIRE((unsigned char)dstbuf[6] == 0xcc);
	ATF_REQUIRE(src == NULL);

	/* Null terminated string, internal state, internal dest. buffer. */
	wmemset(srcbuf, 0xcc, sizeof(srcbuf) / sizeof(*srcbuf));
	wcscpy(srcbuf, L"hello");
	src = srcbuf;
	ATF_REQUIRE(wcsrtombs(NULL, (const wchar_t **)&src, 0, NULL) == 5);

	/* Empty source buffer. */
	wmemset(srcbuf, 0xcc, sizeof(srcbuf) / sizeof(*srcbuf));
	srcbuf[0] = L'\0';
	memset(dstbuf, 0xcc, sizeof(dstbuf));
	src = srcbuf;
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(wcsrtombs(dstbuf, (const wchar_t **)&src, sizeof(dstbuf),
	    &s) == 0);
	ATF_REQUIRE(dstbuf[0] == L'\0');

	/* Zero length destination buffer. */
	wmemset(srcbuf, 0xcc, sizeof(srcbuf) / sizeof(*srcbuf));
	wcscpy(srcbuf, L"hello");
	memset(dstbuf, 0xcc, sizeof(dstbuf));
	src = srcbuf;
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(wcsrtombs(dstbuf, (const wchar_t **)&src, 0, &s) == 0);
	ATF_REQUIRE((unsigned char)dstbuf[0] == 0xcc);

	/*
	 * Japanese (EUC) locale.
	 */

	ATF_REQUIRE(strcmp(setlocale(LC_CTYPE, "ja_JP.eucJP"), "ja_JP.eucJP") == 0);
	ATF_REQUIRE(MB_CUR_MAX > 1);

	wmemset(srcbuf, 0xcc, sizeof(srcbuf) / sizeof(*srcbuf));
	srcbuf[0] = 0xA3C1;
	srcbuf[1] = 0x0020;
	srcbuf[2] = 0x0042;
	srcbuf[3] = 0x0020;
	srcbuf[4] = 0xA3C3;
	srcbuf[5] = 0x0000;
	memset(dstbuf, 0xcc, sizeof(dstbuf));
	src = srcbuf;
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(wcsrtombs(dstbuf, (const wchar_t **)&src, sizeof(dstbuf),
	    &s) == 7);
	ATF_REQUIRE(strcmp(dstbuf, "\xA3\xC1 B \xA3\xC3") == 0);
	ATF_REQUIRE((unsigned char)dstbuf[8] == 0xcc);
	ATF_REQUIRE(src == NULL);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, wcsrtombs_test);

	return (atf_no_error());
}
