/*-
 * Copyright (c) 2002 Tim J. Robbins
 * All rights reserved.
 *
 * Copyright (c) 2013 Ed Schouten <ed@FreeBSD.org>
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
 * Test program for mbrtoc16() as specified by ISO/IEC 9899:2011.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <uchar.h>

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

static mbstate_t s;
static char16_t c16;

ATF_TC_WITHOUT_HEAD(mbrtoc16_c_locale_test);
ATF_TC_BODY(mbrtoc16_c_locale_test, tc)
{

	require_lc_ctype("C");

	/* Null wide character, internal state. */
	ATF_REQUIRE(mbrtoc16(&c16, "", 1, NULL) == 0);
	ATF_REQUIRE(c16 == 0);

	/* Null wide character. */
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtoc16(&c16, "", 1, &s) == 0);
	ATF_REQUIRE(c16 == 0);

	/* Latin letter A, internal state. */
	ATF_REQUIRE(mbrtoc16(NULL, 0, 0, NULL) == 0);
	ATF_REQUIRE(mbrtoc16(&c16, "A", 1, NULL) == 1);
	ATF_REQUIRE(c16 == L'A');

	/* Latin letter A. */
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtoc16(&c16, "A", 1, &s) == 1);
	ATF_REQUIRE(c16 == L'A');

	/* Incomplete character sequence. */
	c16 = L'z';
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtoc16(&c16, "", 0, &s) == (size_t)-2);
	ATF_REQUIRE(c16 == L'z');

	/* Check that mbrtoc16() doesn't access the buffer when n == 0. */
	c16 = L'z';
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtoc16(&c16, "", 0, &s) == (size_t)-2);
	ATF_REQUIRE(c16 == L'z');

	/* Check that mbrtoc16() doesn't read ahead too aggressively. */
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtoc16(&c16, "AB", 2, &s) == 1);
	ATF_REQUIRE(c16 == L'A');
	ATF_REQUIRE(mbrtoc16(&c16, "C", 1, &s) == 1);
	ATF_REQUIRE(c16 == L'C');

}

ATF_TC_WITHOUT_HEAD(mbrtoc16_iso_8859_1_test);
ATF_TC_BODY(mbrtoc16_iso_8859_1_test, tc)
{

	require_lc_ctype("en_US.ISO8859-1");

	/* Currency sign. */
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtoc16(&c16, "\xa4", 1, &s) == 1);
	ATF_REQUIRE(c16 == 0xa4);
}

ATF_TC_WITHOUT_HEAD(mbrtoc16_iso_8859_15_test);
ATF_TC_BODY(mbrtoc16_iso_8859_15_test, tc)
{

	require_lc_ctype("en_US.ISO8859-15");

	/* Euro sign. */
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtoc16(&c16, "\xa4", 1, &s) == 1);
	ATF_REQUIRE(c16 == 0x20ac);
}

ATF_TC_WITHOUT_HEAD(mbrtoc16_utf_8_test);
ATF_TC_BODY(mbrtoc16_utf_8_test, tc)
{

	require_lc_ctype("en_US.UTF-8");

	/* Null wide character, internal state. */
	ATF_REQUIRE(mbrtoc16(NULL, 0, 0, NULL) == 0);
	ATF_REQUIRE(mbrtoc16(&c16, "", 1, NULL) == 0);
	ATF_REQUIRE(c16 == 0);

	/* Null wide character. */
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtoc16(&c16, "", 1, &s) == 0);
	ATF_REQUIRE(c16 == 0);

	/* Latin letter A, internal state. */
	ATF_REQUIRE(mbrtoc16(NULL, 0, 0, NULL) == 0);
	ATF_REQUIRE(mbrtoc16(&c16, "A", 1, NULL) == 1);
	ATF_REQUIRE(c16 == L'A');

	/* Latin letter A. */
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtoc16(&c16, "A", 1, &s) == 1);
	ATF_REQUIRE(c16 == L'A');

	/* Incomplete character sequence (zero length). */
	c16 = L'z';
	memset(&s, 0, sizeof(s));
	ATF_REQUIRE(mbrtoc16(&c16, "", 0, &s) == (size_t)-2);
	ATF_REQUIRE(c16 == L'z');

	/* Incomplete character sequence (truncated double-byte). */
	memset(&s, 0, sizeof(s));
	c16 = 0;
	ATF_REQUIRE(mbrtoc16(&c16, "\xc3", 1, &s) == (size_t)-2);

	/* Same as above, but complete. */
	memset(&s, 0, sizeof(s));
	c16 = 0;
	ATF_REQUIRE(mbrtoc16(&c16, "\xc3\x84", 2, &s) == 2);
	ATF_REQUIRE(c16 == 0xc4);

	/* Test restarting behaviour. */
	memset(&s, 0, sizeof(s));
	c16 = 0;
	ATF_REQUIRE(mbrtoc16(&c16, "\xc3", 1, &s) == (size_t)-2);
	ATF_REQUIRE(c16 == 0);
	ATF_REQUIRE(mbrtoc16(&c16, "\xb7", 1, &s) == 1);
	ATF_REQUIRE(c16 == 0xf7);

	/* Surrogate pair. */
	memset(&s, 0, sizeof(s));
	c16 = 0;
	ATF_REQUIRE(mbrtoc16(&c16, "\xf0\x9f\x92\xa9", 4, &s) == 4);
	ATF_REQUIRE(c16 == 0xd83d);
	ATF_REQUIRE(mbrtoc16(&c16, "", 0, &s) == (size_t)-3);
	ATF_REQUIRE(c16 == 0xdca9);

	/* Letter e with acute, precomposed. */
	memset(&s, 0, sizeof(s));
	c16 = 0;
	ATF_REQUIRE(mbrtoc16(&c16, "\xc3\xa9", 2, &s) == 2);
	ATF_REQUIRE(c16 == 0xe9);

	/* Letter e with acute, combined. */
	memset(&s, 0, sizeof(s));
	c16 = 0;
	ATF_REQUIRE(mbrtoc16(&c16, "\x65\xcc\x81", 3, &s) == 1);
	ATF_REQUIRE(c16 == 0x65);
	ATF_REQUIRE(mbrtoc16(&c16, "\xcc\x81", 2, &s) == 2);
	ATF_REQUIRE(c16 == 0x301);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mbrtoc16_c_locale_test);
	ATF_TP_ADD_TC(tp, mbrtoc16_iso_8859_1_test);
	ATF_TP_ADD_TC(tp, mbrtoc16_iso_8859_15_test);
	ATF_TP_ADD_TC(tp, mbrtoc16_utf_8_test);

	return (atf_no_error());
}
