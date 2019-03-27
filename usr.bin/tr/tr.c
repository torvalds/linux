/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1988, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
static const char sccsid[] = "@(#)tr.c	8.2 (Berkeley) 5/4/95";
#endif

#include <sys/types.h>
#include <sys/capsicum.h>

#include <capsicum_helpers.h>
#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "cmap.h"
#include "cset.h"
#include "extern.h"

static STR s1 = { STRING1, NORMAL, 0, OOBCH, 0, { 0, OOBCH }, NULL, NULL };
static STR s2 = { STRING2, NORMAL, 0, OOBCH, 0, { 0, OOBCH }, NULL, NULL };

static struct cset *setup(char *, STR *, int, int);
static void usage(void);

int
main(int argc, char **argv)
{
	static int carray[NCHARS_SB];
	struct cmap *map;
	struct cset *delete, *squeeze;
	int n, *p;
	int Cflag, cflag, dflag, sflag, isstring2;
	wint_t ch, cnt, lastch;

	(void)setlocale(LC_ALL, "");

	if (caph_limit_stdio() == -1)
		err(1, "unable to limit stdio");

	if (caph_enter() < 0)
		err(1, "unable to enter capability mode");

	Cflag = cflag = dflag = sflag = 0;
	while ((ch = getopt(argc, argv, "Ccdsu")) != -1)
		switch((char)ch) {
		case 'C':
			Cflag = 1;
			cflag = 0;
			break;
		case 'c':
			cflag = 1;
			Cflag = 0;
			break;
		case 'd':
			dflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 'u':
			setbuf(stdout, (char *)NULL);
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	switch(argc) {
	case 0:
	default:
		usage();
		/* NOTREACHED */
	case 1:
		isstring2 = 0;
		break;
	case 2:
		isstring2 = 1;
		break;
	}

	/*
	 * tr -ds [-Cc] string1 string2
	 * Delete all characters (or complemented characters) in string1.
	 * Squeeze all characters in string2.
	 */
	if (dflag && sflag) {
		if (!isstring2)
			usage();

		delete = setup(argv[0], &s1, cflag, Cflag);
		squeeze = setup(argv[1], &s2, 0, 0);

		for (lastch = OOBCH; (ch = getwchar()) != WEOF;)
			if (!cset_in(delete, ch) &&
			    (lastch != ch || !cset_in(squeeze, ch))) {
				lastch = ch;
				(void)putwchar(ch);
			}
		if (ferror(stdin))
			err(1, NULL);
		exit(0);
	}

	/*
	 * tr -d [-Cc] string1
	 * Delete all characters (or complemented characters) in string1.
	 */
	if (dflag) {
		if (isstring2)
			usage();

		delete = setup(argv[0], &s1, cflag, Cflag);

		while ((ch = getwchar()) != WEOF)
			if (!cset_in(delete, ch))
				(void)putwchar(ch);
		if (ferror(stdin))
			err(1, NULL);
		exit(0);
	}

	/*
	 * tr -s [-Cc] string1
	 * Squeeze all characters (or complemented characters) in string1.
	 */
	if (sflag && !isstring2) {
		squeeze = setup(argv[0], &s1, cflag, Cflag);

		for (lastch = OOBCH; (ch = getwchar()) != WEOF;)
			if (lastch != ch || !cset_in(squeeze, ch)) {
				lastch = ch;
				(void)putwchar(ch);
			}
		if (ferror(stdin))
			err(1, NULL);
		exit(0);
	}

	/*
	 * tr [-Ccs] string1 string2
	 * Replace all characters (or complemented characters) in string1 with
	 * the character in the same position in string2.  If the -s option is
	 * specified, squeeze all the characters in string2.
	 */
	if (!isstring2)
		usage();

	map = cmap_alloc();
	if (map == NULL)
		err(1, NULL);
	squeeze = cset_alloc();
	if (squeeze == NULL)
		err(1, NULL);

	s1.str = argv[0];

	if (Cflag || cflag) {
		cmap_default(map, OOBCH);
		if ((s2.str = strdup(argv[1])) == NULL)
			errx(1, "strdup(argv[1])");
	} else
		s2.str = argv[1];

	if (!next(&s2))
		errx(1, "empty string2");

	/*
	 * For -s result will contain only those characters defined
	 * as the second characters in each of the toupper or tolower
	 * pairs.
	 */

	/* If string2 runs out of characters, use the last one specified. */
	while (next(&s1)) {
	again:
		if (s1.state == CCLASS_LOWER &&
		    s2.state == CCLASS_UPPER &&
		    s1.cnt == 1 && s2.cnt == 1) {
			do {
				ch = towupper(s1.lastch);
				cmap_add(map, s1.lastch, ch);
				if (sflag && iswupper(ch))
					cset_add(squeeze, ch);
				if (!next(&s1))
					goto endloop;
			} while (s1.state == CCLASS_LOWER && s1.cnt > 1);
			/* skip upper set */
			do {
				if (!next(&s2))
					break;
			} while (s2.state == CCLASS_UPPER && s2.cnt > 1);
			goto again;
		} else if (s1.state == CCLASS_UPPER &&
			   s2.state == CCLASS_LOWER &&
			   s1.cnt == 1 && s2.cnt == 1) {
			do {
				ch = towlower(s1.lastch);
				cmap_add(map, s1.lastch, ch);
				if (sflag && iswlower(ch))
					cset_add(squeeze, ch);
				if (!next(&s1))
					goto endloop;
			} while (s1.state == CCLASS_UPPER && s1.cnt > 1);
			/* skip lower set */
			do {
				if (!next(&s2))
					break;
			} while (s2.state == CCLASS_LOWER && s2.cnt > 1);
			goto again;
		} else {
			cmap_add(map, s1.lastch, s2.lastch);
			if (sflag)
				cset_add(squeeze, s2.lastch);
		}
		(void)next(&s2);
	}
endloop:
	if (cflag || (Cflag && MB_CUR_MAX > 1)) {
		/*
		 * This is somewhat tricky: since the character set is
		 * potentially huge, we need to avoid allocating a map
		 * entry for every character. Our strategy is to set the
		 * default mapping to the last character of string #2
		 * (= the one that gets automatically repeated), then to
		 * add back identity mappings for characters that should
		 * remain unchanged. We don't waste space on identity mappings
		 * for non-characters with the -C option; those are simulated
		 * in the I/O loop.
		 */
		s2.str = argv[1];
		s2.state = NORMAL;
		for (cnt = 0; cnt < WINT_MAX; cnt++) {
			if (Cflag && !iswrune(cnt))
				continue;
			if (cmap_lookup(map, cnt) == OOBCH) {
				if (next(&s2)) {
					cmap_add(map, cnt, s2.lastch);
					if (sflag)
						cset_add(squeeze, s2.lastch);
				}
			} else
				cmap_add(map, cnt, cnt);
			if ((s2.state == EOS || s2.state == INFINITE) &&
			    cnt >= cmap_max(map))
				break;
		}
		cmap_default(map, s2.lastch);
	} else if (Cflag) {
		for (p = carray, cnt = 0; cnt < NCHARS_SB; cnt++) {
			if (cmap_lookup(map, cnt) == OOBCH && iswrune(cnt))
				*p++ = cnt;
			else
				cmap_add(map, cnt, cnt);
		}
		n = p - carray;
		if (Cflag && n > 1)
			(void)mergesort(carray, n, sizeof(*carray), charcoll);

		s2.str = argv[1];
		s2.state = NORMAL;
		for (cnt = 0; cnt < n; cnt++) {
			(void)next(&s2);
			cmap_add(map, carray[cnt], s2.lastch);
			/*
			 * Chars taken from s2 can be different this time
			 * due to lack of complex upper/lower processing,
			 * so fill string2 again to not miss some.
			 */
			if (sflag)
				cset_add(squeeze, s2.lastch);
		}
	}

	cset_cache(squeeze);
	cmap_cache(map);

	if (sflag)
		for (lastch = OOBCH; (ch = getwchar()) != WEOF;) {
			if (!Cflag || iswrune(ch))
				ch = cmap_lookup(map, ch);
			if (lastch != ch || !cset_in(squeeze, ch)) {
				lastch = ch;
				(void)putwchar(ch);
			}
		}
	else
		while ((ch = getwchar()) != WEOF) {
			if (!Cflag || iswrune(ch))
				ch = cmap_lookup(map, ch);
			(void)putwchar(ch);
		}
	if (ferror(stdin))
		err(1, NULL);
	exit (0);
}

static struct cset *
setup(char *arg, STR *str, int cflag, int Cflag)
{
	struct cset *cs;

	cs = cset_alloc();
	if (cs == NULL)
		err(1, NULL);
	str->str = arg;
	while (next(str))
		cset_add(cs, str->lastch);
	if (Cflag)
		cset_addclass(cs, wctype("rune"), true);
	if (cflag || Cflag)
		cset_invert(cs);
	cset_cache(cs);
	return (cs);
}

int
charcoll(const void *a, const void *b)
{
	static char sa[2], sb[2];

	sa[0] = *(const int *)a;
	sb[0] = *(const int *)b;
	return (strcoll(sa, sb));
}

static void
usage(void)
{
	(void)fprintf(stderr, "%s\n%s\n%s\n%s\n",
		"usage: tr [-Ccsu] string1 string2",
		"       tr [-Ccu] -d string1",
		"       tr [-Ccu] -s string1",
		"       tr [-Ccu] -ds string1 string2");
	exit(1);
}
