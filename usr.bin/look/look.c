/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * David Hitz of Auspex Systems, Inc.
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1991, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)look.c	8.2 (Berkeley) 5/4/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * look -- find lines in a sorted list.
 *
 * The man page said that TABs and SPACEs participate in -d comparisons.
 * In fact, they were ignored.  This implements historic practice, not
 * the manual page.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "pathnames.h"

static char _path_words[] = _PATH_WORDS;

#define	EQUAL		0
#define	GREATER		1
#define	LESS		(-1)

static int dflag, fflag;

static char	*binary_search(wchar_t *, unsigned char *, unsigned char *);
static int	 compare(wchar_t *, unsigned char *, unsigned char *);
static char	*linear_search(wchar_t *, unsigned char *, unsigned char *);
static int	 look(wchar_t *, unsigned char *, unsigned char *);
static wchar_t	*prepkey(const char *, wchar_t);
static void	 print_from(wchar_t *, unsigned char *, unsigned char *);

static void usage(void);

static struct option longopts[] = {
	{ "alternative",no_argument,	NULL, 'a' },
	{ "alphanum",	no_argument,	NULL, 'd' },
	{ "ignore-case",no_argument,	NULL, 'i' },
	{ "terminate",	required_argument, NULL, 't'},
	{ NULL,		0,		NULL, 0 },
};

int
main(int argc, char *argv[])
{
	struct stat sb;
	int ch, fd, match;
	wchar_t termchar;
	unsigned char *back, *front;
	unsigned const char *file;
	wchar_t *key;

	(void) setlocale(LC_CTYPE, "");

	file = _path_words;
	termchar = L'\0';
	while ((ch = getopt_long(argc, argv, "+adft:", longopts, NULL)) != -1)
		switch(ch) {
		case 'a':
			/* COMPATIBILITY */
			break;
		case 'd':
			dflag = 1;
			break;
		case 'f':
			fflag = 1;
			break;
		case 't':
			if (mbrtowc(&termchar, optarg, MB_LEN_MAX, NULL) !=
			    strlen(optarg))
				errx(2, "invalid termination character");
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();
	if (argc == 1) 			/* But set -df by default. */
		dflag = fflag = 1;
	key = prepkey(*argv++, termchar);
	if (argc >= 2)
		file = *argv++;

	match = 1;

	do {
		if ((fd = open(file, O_RDONLY, 0)) < 0 || fstat(fd, &sb))
			err(2, "%s", file);
		if ((uintmax_t)sb.st_size > (uintmax_t)SIZE_T_MAX)
			errx(2, "%s: %s", file, strerror(EFBIG));
		if (sb.st_size == 0) {
			close(fd);
			continue;
		}
		if ((front = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_SHARED, fd, (off_t)0)) == MAP_FAILED)
			err(2, "%s", file);
		back = front + sb.st_size;
		match *= (look(key, front, back));
		close(fd);
	} while (argc-- > 2 && (file = *argv++));

	exit(match);
}

static wchar_t *
prepkey(const char *string, wchar_t termchar)
{
	const char *readp;
	wchar_t *key, *writep;
	wchar_t ch;
	size_t clen;

	/*
	 * Reformat search string and convert to wide character representation
	 * to avoid doing it multiple times later.
	 */
	if ((key = malloc(sizeof(wchar_t) * (strlen(string) + 1))) == NULL)
		err(2, NULL);
	readp = string;
	writep = key;
	while ((clen = mbrtowc(&ch, readp, MB_LEN_MAX, NULL)) != 0) {
		if (clen == (size_t)-1 || clen == (size_t)-2)
			errc(2, EILSEQ, NULL);
		if (fflag)
			ch = towlower(ch);
		if (!dflag || iswalnum(ch))
			*writep++ = ch;
		readp += clen;
	}
	*writep = L'\0';
	if (termchar != L'\0' && (writep = wcschr(key, termchar)) != NULL)
		*++writep = L'\0';
	return (key);
}

static int
look(wchar_t *string, unsigned char *front, unsigned char *back)
{

	front = binary_search(string, front, back);
	front = linear_search(string, front, back);

	if (front)
		print_from(string, front, back);
	return (front ? 0 : 1);
}


/*
 * Binary search for "string" in memory between "front" and "back".
 *
 * This routine is expected to return a pointer to the start of a line at
 * *or before* the first word matching "string".  Relaxing the constraint
 * this way simplifies the algorithm.
 *
 * Invariants:
 * 	front points to the beginning of a line at or before the first
 *	matching string.
 *
 * 	back points to the beginning of a line at or after the first
 *	matching line.
 *
 * Base of the Invariants.
 * 	front = NULL;
 *	back = EOF;
 *
 * Advancing the Invariants:
 *
 * 	p = first newline after halfway point from front to back.
 *
 * 	If the string at "p" is not greater than the string to match,
 *	p is the new front.  Otherwise it is the new back.
 *
 * Termination:
 *
 * 	The definition of the routine allows it return at any point,
 *	since front is always at or before the line to print.
 *
 * 	In fact, it returns when the chosen "p" equals "back".  This
 *	implies that there exists a string is least half as long as
 *	(back - front), which in turn implies that a linear search will
 *	be no more expensive than the cost of simply printing a string or two.
 *
 * 	Trying to continue with binary search at this point would be
 *	more trouble than it's worth.
 */
#define	SKIP_PAST_NEWLINE(p, back) \
	while (p < back && *p++ != '\n');

static char *
binary_search(wchar_t *string, unsigned char *front, unsigned char *back)
{
	unsigned char *p;

	p = front + (back - front) / 2;
	SKIP_PAST_NEWLINE(p, back);

	/*
	 * If the file changes underneath us, make sure we don't
	 * infinitely loop.
	 */
	while (p < back && back > front) {
		if (compare(string, p, back) == GREATER)
			front = p;
		else
			back = p;
		p = front + (back - front) / 2;
		SKIP_PAST_NEWLINE(p, back);
	}
	return (front);
}

/*
 * Find the first line that starts with string, linearly searching from front
 * to back.
 *
 * Return NULL for no such line.
 *
 * This routine assumes:
 *
 * 	o front points at the first character in a line.
 *	o front is before or at the first line to be printed.
 */
static char *
linear_search(wchar_t *string, unsigned char *front, unsigned char *back)
{
	while (front < back) {
		switch (compare(string, front, back)) {
		case EQUAL:		/* Found it. */
			return (front);
		case LESS:		/* No such string. */
			return (NULL);
		case GREATER:		/* Keep going. */
			break;
		}
		SKIP_PAST_NEWLINE(front, back);
	}
	return (NULL);
}

/*
 * Print as many lines as match string, starting at front.
 */
static void
print_from(wchar_t *string, unsigned char *front, unsigned char *back)
{
	for (; front < back && compare(string, front, back) == EQUAL; ++front) {
		for (; front < back && *front != '\n'; ++front)
			if (putchar(*front) == EOF)
				err(2, "stdout");
		if (putchar('\n') == EOF)
			err(2, "stdout");
	}
}

/*
 * Return LESS, GREATER, or EQUAL depending on how the string1 compares with
 * string2 (s1 ??? s2).
 *
 * 	o Matches up to len(s1) are EQUAL.
 *	o Matches up to len(s2) are GREATER.
 *
 * Compare understands about the -f and -d flags, and treats comparisons
 * appropriately.
 *
 * The string "s1" is null terminated.  The string s2 is '\n' terminated (or
 * "back" terminated).
 */
static int
compare(wchar_t *s1, unsigned char *s2, unsigned char *back)
{
	wchar_t ch1, ch2;
	size_t len2;

	for (; *s1 && s2 < back && *s2 != '\n'; ++s1, s2 += len2) {
		ch1 = *s1;
		len2 = mbrtowc(&ch2, s2, back - s2, NULL);
		if (len2 == (size_t)-1 || len2 == (size_t)-2) {
			ch2 = *s2;
			len2 = 1;
		}
		if (fflag)
			ch2 = towlower(ch2);
		if (dflag && !iswalnum(ch2)) {
			/* Ignore character in comparison. */
			--s1;
			continue;
		}
		if (ch1 != ch2)
			return (ch1 < ch2 ? LESS : GREATER);
	}
	return (*s1 ? GREATER : EQUAL);
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: look [-df] [-t char] string [file ...]\n");
	exit(2);
}
