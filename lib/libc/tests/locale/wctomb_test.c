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
 * Test program for wctomb(), as specified by IEEE Std. 1003.1-2001 and
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

#include <atf-c.h>

ATF_TC_WITHOUT_HEAD(wctomb_test);
ATF_TC_BODY(wctomb_test, tc)
{
	size_t len;
	char buf[MB_LEN_MAX + 1];

	/* C/POSIX locale. */

	ATF_REQUIRE(MB_CUR_MAX == 1);

	/* No shift states in C locale. */
	ATF_REQUIRE(wctomb(NULL, L'\0') == 0);

	/* Null wide character. */
	memset(buf, 0xcc, sizeof(buf));
	len = wctomb(buf, L'\0');
	ATF_REQUIRE(len == 1);
	ATF_REQUIRE((unsigned char)buf[0] == 0 && (unsigned char)buf[1] == 0xcc);

	/* Latin letter A. */
	memset(buf, 0xcc, sizeof(buf));
	len = wctomb(buf, L'A');
	ATF_REQUIRE(len == 1);
	ATF_REQUIRE((unsigned char)buf[0] == 'A' && (unsigned char)buf[1] == 0xcc);

	/* Invalid code. */
	ATF_REQUIRE(wctomb(buf, UCHAR_MAX + 1) == -1);
	ATF_REQUIRE(wctomb(NULL, 0) == 0);

	/*
	 * Japanese (EUC) locale.
	 */

	ATF_REQUIRE(strcmp(setlocale(LC_CTYPE, "ja_JP.eucJP"), "ja_JP.eucJP") == 0);
	ATF_REQUIRE(MB_CUR_MAX == 3);

	/* No shift states in EUC encoding. */
	ATF_REQUIRE(wctomb(NULL, L'\0') == 0);

	/* Null wide character. */
	memset(buf, 0xcc, sizeof(buf));
	len = wctomb(buf, L'\0');
	ATF_REQUIRE(len == 1);
	ATF_REQUIRE((unsigned char)buf[0] == 0 && (unsigned char)buf[1] == 0xcc);

	/* Latin letter A. */
	memset(buf, 0xcc, sizeof(buf));
	len = wctomb(buf, L'A');
	ATF_REQUIRE(len == 1);
	ATF_REQUIRE((unsigned char)buf[0] == 'A' && (unsigned char)buf[1] == 0xcc);

	/* Full width letter A. */
	memset(buf, 0xcc, sizeof(buf));
	len = wctomb(buf, 0xa3c1);
	ATF_REQUIRE(len == 2);
	ATF_REQUIRE((unsigned char)buf[0] == 0xa3 &&
		(unsigned char)buf[1] == 0xc1 &&
		(unsigned char)buf[2] == 0xcc);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, wctomb_test);

	return (atf_no_error());
}
