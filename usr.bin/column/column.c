/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1994
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1989, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#if 0
#ifndef lint
static char sccsid[] = "@(#)column.c	8.4 (Berkeley) 5/4/95";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/param.h>

#include <err.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define	TAB	8

static void	c_columnate(void);
static void	input(FILE *);
static void	maketbl(void);
static void	print(void);
static void	r_columnate(void);
static void	usage(void);
static int	width(const wchar_t *);

static int	termwidth = 80;		/* default terminal width */

static int	entries;		/* number of records */
static int	eval;			/* exit value */
static int	maxlength;		/* longest record */
static wchar_t	**list;			/* array of pointers to records */
static const wchar_t *separator = L"\t "; /* field separator for table option */

int
main(int argc, char **argv)
{
	struct winsize win;
	FILE *fp;
	int ch, tflag, xflag;
	char *p;
	const char *src;
	wchar_t *newsep;
	size_t seplen;

	setlocale(LC_ALL, "");

	if (ioctl(1, TIOCGWINSZ, &win) == -1 || !win.ws_col) {
		if ((p = getenv("COLUMNS")))
			termwidth = atoi(p);
	} else
		termwidth = win.ws_col;

	tflag = xflag = 0;
	while ((ch = getopt(argc, argv, "c:s:tx")) != -1)
		switch(ch) {
		case 'c':
			termwidth = atoi(optarg);
			break;
		case 's':
			src = optarg;
			seplen = mbsrtowcs(NULL, &src, 0, NULL);
			if (seplen == (size_t)-1)
				err(1, "bad separator");
			newsep = malloc((seplen + 1) * sizeof(wchar_t));
			if (newsep == NULL)
				err(1, NULL);
			mbsrtowcs(newsep, &src, seplen + 1, NULL);
			separator = newsep;
			break;
		case 't':
			tflag = 1;
			break;
		case 'x':
			xflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (!*argv)
		input(stdin);
	else for (; *argv; ++argv)
		if ((fp = fopen(*argv, "r"))) {
			input(fp);
			(void)fclose(fp);
		} else {
			warn("%s", *argv);
			eval = 1;
		}

	if (!entries)
		exit(eval);

	maxlength = roundup(maxlength + 1, TAB);
	if (tflag)
		maketbl();
	else if (maxlength >= termwidth)
		print();
	else if (xflag)
		c_columnate();
	else
		r_columnate();
	exit(eval);
}

static void
c_columnate(void)
{
	int chcnt, col, cnt, endcol, numcols;
	wchar_t **lp;

	numcols = termwidth / maxlength;
	endcol = maxlength;
	for (chcnt = col = 0, lp = list;; ++lp) {
		wprintf(L"%ls", *lp);
		chcnt += width(*lp);
		if (!--entries)
			break;
		if (++col == numcols) {
			chcnt = col = 0;
			endcol = maxlength;
			putwchar('\n');
		} else {
			while ((cnt = roundup(chcnt + 1, TAB)) <= endcol) {
				(void)putwchar('\t');
				chcnt = cnt;
			}
			endcol += maxlength;
		}
	}
	if (chcnt)
		putwchar('\n');
}

static void
r_columnate(void)
{
	int base, chcnt, cnt, col, endcol, numcols, numrows, row;

	numcols = termwidth / maxlength;
	numrows = entries / numcols;
	if (entries % numcols)
		++numrows;

	for (row = 0; row < numrows; ++row) {
		endcol = maxlength;
		for (base = row, chcnt = col = 0; col < numcols; ++col) {
			wprintf(L"%ls", list[base]);
			chcnt += width(list[base]);
			if ((base += numrows) >= entries)
				break;
			while ((cnt = roundup(chcnt + 1, TAB)) <= endcol) {
				(void)putwchar('\t');
				chcnt = cnt;
			}
			endcol += maxlength;
		}
		putwchar('\n');
	}
}

static void
print(void)
{
	int cnt;
	wchar_t **lp;

	for (cnt = entries, lp = list; cnt--; ++lp)
		(void)wprintf(L"%ls\n", *lp);
}

typedef struct _tbl {
	wchar_t **list;
	int cols, *len;
} TBL;
#define	DEFCOLS	25

static void
maketbl(void)
{
	TBL *t;
	int coloff, cnt;
	wchar_t *p, **lp;
	int *lens, maxcols;
	TBL *tbl;
	wchar_t **cols;
	wchar_t *last;

	if ((t = tbl = calloc(entries, sizeof(TBL))) == NULL)
		err(1, NULL);
	if ((cols = calloc((maxcols = DEFCOLS), sizeof(*cols))) == NULL)
		err(1, NULL);
	if ((lens = calloc(maxcols, sizeof(int))) == NULL)
		err(1, NULL);
	for (cnt = 0, lp = list; cnt < entries; ++cnt, ++lp, ++t) {
		for (coloff = 0, p = *lp;
		    (cols[coloff] = wcstok(p, separator, &last));
		    p = NULL)
			if (++coloff == maxcols) {
				if (!(cols = realloc(cols, ((u_int)maxcols +
				    DEFCOLS) * sizeof(wchar_t *))) ||
				    !(lens = realloc(lens,
				    ((u_int)maxcols + DEFCOLS) * sizeof(int))))
					err(1, NULL);
				memset((char *)lens + maxcols * sizeof(int),
				    0, DEFCOLS * sizeof(int));
				maxcols += DEFCOLS;
			}
		if ((t->list = calloc(coloff, sizeof(*t->list))) == NULL)
			err(1, NULL);
		if ((t->len = calloc(coloff, sizeof(int))) == NULL)
			err(1, NULL);
		for (t->cols = coloff; --coloff >= 0;) {
			t->list[coloff] = cols[coloff];
			t->len[coloff] = width(cols[coloff]);
			if (t->len[coloff] > lens[coloff])
				lens[coloff] = t->len[coloff];
		}
	}
	for (cnt = 0, t = tbl; cnt < entries; ++cnt, ++t) {
		for (coloff = 0; coloff < t->cols  - 1; ++coloff)
			(void)wprintf(L"%ls%*ls", t->list[coloff],
			    lens[coloff] - t->len[coloff] + 2, L" ");
		(void)wprintf(L"%ls\n", t->list[coloff]);
		free(t->list);
		free(t->len);
	}
	free(lens);
	free(cols);
	free(tbl);
}

#define	DEFNUM		1000
#define	MAXLINELEN	(LINE_MAX + 1)

static void
input(FILE *fp)
{
	static int maxentry;
	int len;
	wchar_t *p, buf[MAXLINELEN];

	if (!list)
		if ((list = calloc((maxentry = DEFNUM), sizeof(*list))) ==
		    NULL)
			err(1, NULL);
	while (fgetws(buf, MAXLINELEN, fp)) {
		for (p = buf; *p && iswspace(*p); ++p);
		if (!*p)
			continue;
		if (!(p = wcschr(p, L'\n'))) {
			warnx("line too long");
			eval = 1;
			continue;
		}
		*p = L'\0';
		len = width(buf);
		if (maxlength < len)
			maxlength = len;
		if (entries == maxentry) {
			maxentry += DEFNUM;
			if (!(list = realloc(list,
			    (u_int)maxentry * sizeof(*list))))
				err(1, NULL);
		}
		list[entries] = malloc((wcslen(buf) + 1) * sizeof(wchar_t));
		if (list[entries] == NULL)
			err(1, NULL);
		wcscpy(list[entries], buf);
		entries++;
	}
}

/* Like wcswidth(), but ignores non-printing characters. */
static int
width(const wchar_t *wcs)
{
	int w, cw;

	for (w = 0; *wcs != L'\0'; wcs++)
		if ((cw = wcwidth(*wcs)) > 0)
			w += cw;
	return (w);
}

static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: column [-tx] [-c columns] [-s sep] [file ...]\n");
	exit(1);
}
