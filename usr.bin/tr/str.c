/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
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
static const char sccsid[] = "@(#)str.c	8.2 (Berkeley) 4/28/95";
#endif

#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "extern.h"

static int      backslash(STR *, int *);
static int	bracket(STR *);
static void	genclass(STR *);
static void	genequiv(STR *);
static int      genrange(STR *, int);
static void	genseq(STR *);

wint_t
next(STR *s)
{
	int is_octal;
	wint_t ch;
	wchar_t wch;
	size_t clen;

	switch (s->state) {
	case EOS:
		return (0);
	case INFINITE:
		return (1);
	case NORMAL:
		switch (*s->str) {
		case '\0':
			s->state = EOS;
			return (0);
		case '\\':
			s->lastch = backslash(s, &is_octal);
			break;
		case '[':
			if (bracket(s))
				return (next(s));
			/* FALLTHROUGH */
		default:
			clen = mbrtowc(&wch, s->str, MB_LEN_MAX, NULL);
			if (clen == (size_t)-1 || clen == (size_t)-2 ||
			    clen == 0)
				errc(1, EILSEQ, NULL);
			is_octal = 0;
			s->lastch = wch;
			s->str += clen;
			break;
		}

		/* We can start a range at any time. */
		if (s->str[0] == '-' && genrange(s, is_octal))
			return (next(s));
		return (1);
	case RANGE:
		if (s->cnt-- == 0) {
			s->state = NORMAL;
			return (next(s));
		}
		++s->lastch;
		return (1);
	case SEQUENCE:
		if (s->cnt-- == 0) {
			s->state = NORMAL;
			return (next(s));
		}
		return (1);
	case CCLASS:
	case CCLASS_UPPER:
	case CCLASS_LOWER:
		s->cnt++;
		ch = nextwctype(s->lastch, s->cclass);
		if (ch == -1) {
			s->state = NORMAL;
			return (next(s));
		}
		s->lastch = ch;
		return (1);
	case SET:
		if ((ch = s->set[s->cnt++]) == OOBCH) {
			s->state = NORMAL;
			return (next(s));
		}
		s->lastch = ch;
		return (1);
	default:
		return (0);
	}
	/* NOTREACHED */
}

static int
bracket(STR *s)
{
	char *p;

	switch (s->str[1]) {
	case ':':				/* "[:class:]" */
		if ((p = strchr(s->str + 2, ']')) == NULL)
			return (0);
		if (*(p - 1) != ':' || p - s->str < 4)
			goto repeat;
		*(p - 1) = '\0';
		s->str += 2;
		genclass(s);
		s->str = p + 1;
		return (1);
	case '=':				/* "[=equiv=]" */
		if (s->str[2] == '\0' || (p = strchr(s->str + 3, ']')) == NULL)
			return (0);
		if (*(p - 1) != '=' || p - s->str < 4)
			goto repeat;
		s->str += 2;
		genequiv(s);
		return (1);
	default:				/* "[\###*n]" or "[#*n]" */
	repeat:
		if ((p = strpbrk(s->str + 2, "*]")) == NULL)
			return (0);
		if (p[0] != '*' || strchr(p, ']') == NULL)
			return (0);
		s->str += 1;
		genseq(s);
		return (1);
	}
	/* NOTREACHED */
}

static void
genclass(STR *s)
{

	if ((s->cclass = wctype(s->str)) == 0)
		errx(1, "unknown class %s", s->str);
	s->cnt = 0;
	s->lastch = -1;		/* incremented before check in next() */
	if (strcmp(s->str, "upper") == 0)
		s->state = CCLASS_UPPER;
	else if (strcmp(s->str, "lower") == 0)
		s->state = CCLASS_LOWER;
	else
		s->state = CCLASS;
}

static void
genequiv(STR *s)
{
	int i, p, pri;
	char src[2], dst[3];
	size_t clen;
	wchar_t wc;

	if (*s->str == '\\') {
		s->equiv[0] = backslash(s, NULL);
		if (*s->str != '=')
			errx(1, "misplaced equivalence equals sign");
		s->str += 2;
	} else {
		clen = mbrtowc(&wc, s->str, MB_LEN_MAX, NULL);
		if (clen == (size_t)-1 || clen == (size_t)-2 || clen == 0)
			errc(1, EILSEQ, NULL);
		s->equiv[0] = wc;
		if (s->str[clen] != '=')
			errx(1, "misplaced equivalence equals sign");
		s->str += clen + 2;
	}

	/*
	 * Calculate the set of all characters in the same equivalence class
	 * as the specified character (they will have the same primary
	 * collation weights).
	 * XXX Knows too much about how strxfrm() is implemented. Assumes
	 * it fills the string with primary collation weight bytes. Only one-
	 * to-one mappings are supported.
	 * XXX Equivalence classes not supported in multibyte locales.
	 */
	src[0] = (char)s->equiv[0];
	src[1] = '\0';
	if (MB_CUR_MAX == 1 && strxfrm(dst, src, sizeof(dst)) == 1) {
		pri = (unsigned char)*dst;
		for (p = 1, i = 1; i < NCHARS_SB; i++) {
			*src = i;
			if (strxfrm(dst, src, sizeof(dst)) == 1 && pri &&
			    pri == (unsigned char)*dst)
				s->equiv[p++] = i;
		}
		s->equiv[p] = OOBCH;
	}

	s->cnt = 0;
	s->state = SET;
	s->set = s->equiv;
}

static int
genrange(STR *s, int was_octal)
{
	int stopval, octal;
	char *savestart;
	int n, cnt, *p;
	size_t clen;
	wchar_t wc;

	octal = 0;
	savestart = s->str;
	if (*++s->str == '\\')
		stopval = backslash(s, &octal);
	else {
		clen = mbrtowc(&wc, s->str, MB_LEN_MAX, NULL);
		if (clen == (size_t)-1 || clen == (size_t)-2)
			errc(1, EILSEQ, NULL);
		stopval = wc;
		s->str += clen;
	}
	/*
	 * XXX Characters are not ordered according to collating sequence in
	 * multibyte locales.
	 */
	if (octal || was_octal || MB_CUR_MAX > 1) {
		if (stopval < s->lastch) {
			s->str = savestart;
			return (0);
		}
		s->cnt = stopval - s->lastch + 1;
		s->state = RANGE;
		--s->lastch;
		return (1);
	}
	if (charcoll((const void *)&stopval, (const void *)&(s->lastch)) < 0) {
		s->str = savestart;
		return (0);
	}
	if ((s->set = p = malloc((NCHARS_SB + 1) * sizeof(int))) == NULL)
		err(1, "genrange() malloc");
	for (cnt = 0; cnt < NCHARS_SB; cnt++)
		if (charcoll((const void *)&cnt, (const void *)&(s->lastch)) >= 0 &&
		    charcoll((const void *)&cnt, (const void *)&stopval) <= 0)
			*p++ = cnt;
	*p = OOBCH;
	n = p - s->set;

	s->cnt = 0;
	s->state = SET;
	if (n > 1)
		mergesort(s->set, n, sizeof(*(s->set)), charcoll);
	return (1);
}

static void
genseq(STR *s)
{
	char *ep;
	wchar_t wc;
	size_t clen;

	if (s->which == STRING1)
		errx(1, "sequences only valid in string2");

	if (*s->str == '\\')
		s->lastch = backslash(s, NULL);
	else {
		clen = mbrtowc(&wc, s->str, MB_LEN_MAX, NULL);
		if (clen == (size_t)-1 || clen == (size_t)-2)
			errc(1, EILSEQ, NULL);
		s->lastch = wc;
		s->str += clen;
	}
	if (*s->str != '*')
		errx(1, "misplaced sequence asterisk");

	switch (*++s->str) {
	case '\\':
		s->cnt = backslash(s, NULL);
		break;
	case ']':
		s->cnt = 0;
		++s->str;
		break;
	default:
		if (isdigit((u_char)*s->str)) {
			s->cnt = strtol(s->str, &ep, 0);
			if (*ep == ']') {
				s->str = ep + 1;
				break;
			}
		}
		errx(1, "illegal sequence count");
		/* NOTREACHED */
	}

	s->state = s->cnt ? SEQUENCE : INFINITE;
}

/*
 * Translate \??? into a character.  Up to 3 octal digits, if no digits either
 * an escape code or a literal character.
 */
static int
backslash(STR *s, int *is_octal)
{
	int ch, cnt, val;

	if (is_octal != NULL)
		*is_octal = 0;
	for (cnt = val = 0;;) {
		ch = (u_char)*++s->str;
		if (!isdigit(ch) || ch > '7')
			break;
		val = val * 8 + ch - '0';
		if (++cnt == 3) {
			++s->str;
			break;
		}
	}
	if (cnt) {
		if (is_octal != NULL)
			*is_octal = 1;
		return (val);
	}
	if (ch != '\0')
		++s->str;
	switch (ch) {
		case 'a':			/* escape characters */
			return ('\7');
		case 'b':
			return ('\b');
		case 'f':
			return ('\f');
		case 'n':
			return ('\n');
		case 'r':
			return ('\r');
		case 't':
			return ('\t');
		case 'v':
			return ('\13');
		case '\0':			/*  \" -> \ */
			s->state = EOS;
			return ('\\');
		default:			/* \x" -> x */
			return (ch);
	}
}
