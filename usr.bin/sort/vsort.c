/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012 Oleg Moskalenko <mom040267@gmail.com>
 * Copyright (C) 2012 Gabor Kovesdan <gabor@FreeBSD.org>
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

#include <sys/types.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "sort.h"
#include "vsort.h"

static inline bool
isdigit_clocale(wchar_t c)
{

	return (c >= L'0' && c <= L'9');
}

static inline bool
isalpha_clocale(wchar_t c)
{

	return ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'));
}

static inline bool
isalnum_clocale(wchar_t c)
{

	return ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
	    (c >= L'0' && c <= L'9'));
}

/*
 * Find string suffix of format: (\.[A-Za-z~][A-Za-z0-9~]*)*$
 * Set length of string before suffix.
 */
static void
find_suffix(bwstring_iterator si, bwstring_iterator se, size_t *len)
{
	wchar_t c;
	size_t clen;
	bool expect_alpha, sfx;

	sfx = false;
	expect_alpha = false;
	*len = 0;
	clen = 0;

	while ((si < se) && (c = bws_get_iter_value(si))) {
		if (expect_alpha) {
			expect_alpha = false;
			if (!isalpha_clocale(c) && (c != L'~'))
				sfx = false;
		} else if (c == L'.') {
			expect_alpha = true;
			if (!sfx) {
				sfx = true;
				*len = clen;
			}
		} else if (!isalnum_clocale(c) && (c != L'~'))
			sfx = false;

		si = bws_iterator_inc(si, 1);
		++clen;
	}

	/* This code must be here to make the implementation compatible
	 * with WORDING of GNU sort documentation.
	 * But the GNU sort implementation is not following its own
	 * documentation.  GNU sort allows empty file extensions
	 * (just dot with nothing after); but the regular expression in
	 * their documentation does not allow empty file extensions.
	 * We chose to make our implementation compatible with GNU sort
	 * implementation.  If they will ever fix their bug, this code
	 * must be uncommented. Or they may choose to fix the info page,
	 * then the code stays commented.
	 *
	 if (expect_alpha)
	 	sfx = false;
	 */

	if (!sfx)
		*len = clen;
}

static inline int
cmp_chars(wchar_t c1, wchar_t c2)
{

	if (c1 == c2)
		return (0);

	if (c1 == L'~')
		return (-1);
	if (c2 == L'~')
		return (+1);

	if (isdigit_clocale(c1) || !c1)
		return ((isdigit_clocale(c2) || !c2) ? 0 : -1);

	if (isdigit_clocale(c2) || !c2)
		return (+1);

	if (isalpha_clocale(c1))
		return ((isalpha_clocale(c2)) ? ((int) c1 - (int) c2) : -1);

	if (isalpha_clocale(c2))
		return (+1);

	return ((int) c1 - (int) c2);
}

static int
cmpversions(bwstring_iterator si1, bwstring_iterator se1,
    bwstring_iterator si2, bwstring_iterator se2)
{
	int cmp, diff;

	while ((si1 < se1) || (si2 < se2)) {
		diff = 0;

		while (((si1 < se1) &&
		    !isdigit_clocale(bws_get_iter_value(si1))) ||
		    ((si2 < se2) && !isdigit_clocale(bws_get_iter_value(si2)))) {
			wchar_t c1, c2;

			c1 = (si1 < se1) ? bws_get_iter_value(si1) : 0;
			c2 = (si2 < se2) ? bws_get_iter_value(si2) : 0;

			cmp = cmp_chars(c1, c2);
			if (cmp)
				return (cmp);

			if (si1 < se1)
				si1 = bws_iterator_inc(si1, 1);
			if (si2 < se2)
				si2 = bws_iterator_inc(si2, 1);
		}

		while (bws_get_iter_value(si1) == L'0')
			si1 = bws_iterator_inc(si1, 1);

		while (bws_get_iter_value(si2) == L'0')
			si2 = bws_iterator_inc(si2, 1);

		while (isdigit_clocale(bws_get_iter_value(si1)) &&
		    isdigit_clocale(bws_get_iter_value(si2))) {
			if (!diff)
				diff = ((int)bws_get_iter_value(si1) -
				    (int)bws_get_iter_value(si2));
			si1 = bws_iterator_inc(si1, 1);
			si2 = bws_iterator_inc(si2, 1);
		}

		if (isdigit_clocale(bws_get_iter_value(si1)))
			return (1);

		if (isdigit_clocale(bws_get_iter_value(si2)))
			return (-1);

		if (diff)
			return (diff);
	}

	return (0);
}

/*
 * Compare two version strings
 */
int
vcmp(struct bwstring *s1, struct bwstring *s2)
{
	bwstring_iterator si1, si2;
	wchar_t c1, c2;
	size_t len1, len2, slen1, slen2;
	int cmp_bytes, cmp_res;

	if (s1 == s2)
		return (0);

	cmp_bytes = bwscmp(s1, s2, 0);
	if (cmp_bytes == 0)
		return (0);

	len1 = slen1 = BWSLEN(s1);
	len2 = slen2 = BWSLEN(s2);

	if (slen1 < 1)
		return (-1);
	if (slen2 < 1)
		return (+1);

	si1 = bws_begin(s1);
	si2 = bws_begin(s2);

	c1 = bws_get_iter_value(si1);
	c2 = bws_get_iter_value(si2);

	if (c1 == L'.' && (slen1 == 1))
		return (-1);

	if (c2 == L'.' && (slen2 == 1))
		return (+1);

	if (slen1 == 2 && c1 == L'.' &&
	    bws_get_iter_value(bws_iterator_inc(si1, 1)) == L'.')
		return (-1);
	if (slen2 == 2 && c2 == L'.' &&
	    bws_get_iter_value(bws_iterator_inc(si2, 1)) == L'.')
		return (+1);

	if (c1 == L'.' && c2 != L'.')
		return (-1);
	if (c1 != L'.' && c2 == L'.')
		return (+1);

	if (c1 == L'.' && c2 == L'.') {
		si1 = bws_iterator_inc(si1, 1);
		si2 = bws_iterator_inc(si2, 1);
	}

	find_suffix(si1, bws_end(s1), &len1);
	find_suffix(si2, bws_end(s2), &len2);

	if ((len1 == len2) && (bws_iterator_cmp(si1, si2, len1) == 0))
		return (cmp_bytes);

	cmp_res = cmpversions(si1, bws_iterator_inc(si1, len1), si2,
	    bws_iterator_inc(si2, len2));

	if (cmp_res == 0)
	  cmp_res = cmp_bytes;

	return (cmp_res);
}
