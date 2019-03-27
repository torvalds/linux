/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kevin Ruddy.
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
"@(#) Copyright (c) 1990, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)fold.c	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define	DEFLINEWIDTH	80

void fold(int);
static int newpos(int, wint_t);
static void usage(void);

static int bflag;		/* Count bytes, not columns */
static int sflag;		/* Split on word boundaries */

int
main(int argc, char **argv)
{
	int ch, previous_ch;
	int rval, width;

	(void) setlocale(LC_CTYPE, "");

	width = -1;
	previous_ch = 0;
	while ((ch = getopt(argc, argv, "0123456789bsw:")) != -1) {
		switch (ch) {
		case 'b':
			bflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 'w':
			if ((width = atoi(optarg)) <= 0) {
				errx(1, "illegal width value");
			}
			break;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			/* Accept a width as eg. -30. Note that a width
			 * specified using the -w option is always used prior
			 * to this undocumented option. */
			switch (previous_ch) {
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				/* The width is a number with multiple digits:
				 * add the last one. */
				width = width * 10 + (ch - '0');
				break;
			default:
				/* Set the width, unless it was previously
				 * set. For instance, the following options
				 * would all give a width of 5 and not 10:
				 *   -10 -w5
				 *   -5b10
				 *   -5 -10b */
				if (width == -1)
					width = ch - '0';
				break;
			}
			break;
		default:
			usage();
		}
		previous_ch = ch;
	}
	argv += optind;
	argc -= optind;

	if (width == -1)
		width = DEFLINEWIDTH;
	rval = 0;
	if (!*argv)
		fold(width);
	else for (; *argv; ++argv)
		if (!freopen(*argv, "r", stdin)) {
			warn("%s", *argv);
			rval = 1;
		} else
			fold(width);
	exit(rval);
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: fold [-bs] [-w width] [file ...]\n");
	exit(1);
}

/*
 * Fold the contents of standard input to fit within WIDTH columns (or bytes)
 * and write to standard output.
 *
 * If sflag is set, split the line at the last space character on the line.
 * This flag necessitates storing the line in a buffer until the current
 * column > width, or a newline or EOF is read.
 *
 * The buffer can grow larger than WIDTH due to backspaces and carriage
 * returns embedded in the input stream.
 */
void
fold(int width)
{
	static wchar_t *buf;
	static int buf_max;
	int col, i, indx, space;
	wint_t ch;

	col = indx = 0;
	while ((ch = getwchar()) != WEOF) {
		if (ch == '\n') {
			wprintf(L"%.*ls\n", indx, buf);
			col = indx = 0;
			continue;
		}
		if ((col = newpos(col, ch)) > width) {
			if (sflag) {
				i = indx;
				while (--i >= 0 && !iswblank(buf[i]))
					;
				space = i;
			}
			if (sflag && space != -1) {
				space++;
				wprintf(L"%.*ls\n", space, buf);
				wmemmove(buf, buf + space, indx - space);
				indx -= space;
				col = 0;
				for (i = 0; i < indx; i++)
					col = newpos(col, buf[i]);
			} else {
				wprintf(L"%.*ls\n", indx, buf);
				col = indx = 0;
			}
			col = newpos(col, ch);
		}
		if (indx + 1 > buf_max) {
			buf_max += LINE_MAX;
			buf = realloc(buf, sizeof(*buf) * buf_max);
			if (buf == NULL)
				err(1, "realloc()");
		}
		buf[indx++] = ch;
	}

	if (indx != 0)
		wprintf(L"%.*ls", indx, buf);
}

/*
 * Update the current column position for a character.
 */
static int
newpos(int col, wint_t ch)
{
	char buf[MB_LEN_MAX];
	size_t len;
	int w;

	if (bflag) {
		len = wcrtomb(buf, ch, NULL);
		col += len;
	} else
		switch (ch) {
		case '\b':
			if (col > 0)
				--col;
			break;
		case '\r':
			col = 0;
			break;
		case '\t':
			col = (col + 8) & ~7;
			break;
		default:
			if ((w = wcwidth(ch)) > 0)
				col += w;
			break;
		}

	return (col);
}
