/*-
 * Copyright (c) 2016 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright 2016 Tom Lane <tgl@sss.pgh.pa.us>
 * Copyright 2017 Nexenta Systems, Inc.
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

#include <wchar.h>
#include <locale.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#include <atf-c.h>

static int
cmp(const void *a, const void *b)
{
	const wchar_t wa[2] = { *(const wchar_t *)a, 0 };
	const wchar_t wb[2] = { *(const wchar_t *)b, 0 };

	return (wcscoll(wa, wb));
}

ATF_TC_WITHOUT_HEAD(russian_collation);
ATF_TC_BODY(russian_collation, tc)
{
	wchar_t c[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyzЁАБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмнопрстуфхцчшщъыьэюяё";
	wchar_t res[] = L"aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZаАбБвВгГдДеЕёЁжЖзЗиИйЙкКлЛмМнНоОпПрРсСтТуУфФхХцЦчЧшШщЩъЪыЫьЬэЭюЮяЯ";

	ATF_CHECK_MSG(setlocale(LC_ALL, "ru_RU.UTF-8") != NULL,
	    "Fail to set locale to \"ru_RU.UTF-8\"");
	qsort(c, wcslen(c), sizeof(wchar_t), cmp);
	ATF_CHECK_MSG(wcscmp(c, res) == 0,
	    "Bad collation, expected: '%ls' got '%ls'", res, c);
}

#define	NSTRINGS 2000
#define	MAXSTRLEN 20
#define	MAXXFRMLEN (MAXSTRLEN * 20)

typedef struct {
	char	sval[MAXSTRLEN];
	char	xval[MAXXFRMLEN];
} cstr;

ATF_TC_WITHOUT_HEAD(strcoll_vs_strxfrm);
ATF_TC_BODY(strcoll_vs_strxfrm, tc)
{
	cstr	data[NSTRINGS];
	char	*curloc;
	int	i, j;

	curloc = setlocale(LC_ALL, "en_US.UTF-8");
	ATF_CHECK_MSG(curloc != NULL, "Fail to set locale");

	/* Ensure new random() values on every run */
	srandom((unsigned int) time(NULL));

	/* Generate random UTF8 strings of length less than MAXSTRLEN bytes */
	for (i = 0; i < NSTRINGS; i++) {
		char	*p;
		int	len;

again:
		p = data[i].sval;
		len = 1 + (random() % (MAXSTRLEN - 1));
		while (len > 0) {
			int c;
			/*
			 * Generate random printable char in ISO8859-1 range.
			 * Bias towards producing a lot of spaces.
			 */

			if ((random() % 16) < 3) {
				c = ' ';
			} else {
				do {
					c = random() & 0xFF;
				} while (!((c >= ' ' && c <= 127) ||
				    (c >= 0xA0 && c <= 0xFF)));
			}

			if (c <= 127) {
				*p++ = c;
				len--;
			} else {
				if (len < 2)
					break;
				/* Poor man's utf8-ification */
				*p++ = 0xC0 + (c >> 6);
				len--;
				*p++ = 0x80 + (c & 0x3F);
				len--;
			}
		}
		*p = '\0';
		/* strxfrm() each string as we produce it */
		errno = 0;
		ATF_CHECK_MSG(strxfrm(data[i].xval, data[i].sval,
		    MAXXFRMLEN) < MAXXFRMLEN, "strxfrm() result for %d-length "
		    " string exceeded %d bytes", (int)strlen(data[i].sval),
		    MAXXFRMLEN);

		/*
		 * Amend strxfrm() failing on certain characters to be fixed and
		 * test later
		 */
		if (errno != 0)
			goto again;
	}

	for (i = 0; i < NSTRINGS; i++) {
		for (j = 0; j < NSTRINGS; j++) {
			int sr = strcoll(data[i].sval, data[j].sval);
			int sx = strcmp(data[i].xval, data[j].xval);

			ATF_CHECK_MSG(!((sr * sx < 0) ||
			    (sr * sx == 0 && sr + sx != 0)),
			    "%s: diff for \"%s\" and \"%s\"",
			    curloc, data[i].sval, data[j].sval);
		}
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, russian_collation);
	ATF_TP_ADD_TC(tp, strcoll_vs_strxfrm);

	return (atf_no_error());
}
