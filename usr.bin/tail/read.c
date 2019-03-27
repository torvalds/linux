/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Edward Sze-Tyan Wang.
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
static const char sccsid[] = "@(#)read.c	8.1 (Berkeley) 6/6/93";
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

/*
 * bytes -- read bytes to an offset from the end and display.
 *
 * This is the function that reads to a byte offset from the end of the input,
 * storing the data in a wrap-around buffer which is then displayed.  If the
 * rflag is set, the data is displayed in lines in reverse order, and this
 * routine has the usual nastiness of trying to find the newlines.  Otherwise,
 * it is displayed from the character closest to the beginning of the input to
 * the end.
 */
int
bytes(FILE *fp, const char *fn, off_t off)
{
	int ch, len, tlen;
	char *ep, *p, *t;
	int wrap;
	char *sp;

	if ((sp = p = malloc(off)) == NULL)
		err(1, "malloc");

	for (wrap = 0, ep = p + off; (ch = getc(fp)) != EOF;) {
		*p = ch;
		if (++p == ep) {
			wrap = 1;
			p = sp;
		}
	}
	if (ferror(fp)) {
		ierr(fn);
		free(sp);
		return 1;
	}

	if (rflag) {
		for (t = p - 1, len = 0; t >= sp; --t, ++len)
			if (*t == '\n' && len) {
				WR(t + 1, len);
				len = 0;
		}
		if (wrap) {
			tlen = len;
			for (t = ep - 1, len = 0; t >= p; --t, ++len)
				if (*t == '\n') {
					if (len) {
						WR(t + 1, len);
						len = 0;
					}
					if (tlen) {
						WR(sp, tlen);
						tlen = 0;
					}
				}
			if (len)
				WR(t + 1, len);
			if (tlen)
				WR(sp, tlen);
		}
	} else {
		if (wrap && (len = ep - p))
			WR(p, len);
		len = p - sp;
		if (len)
			WR(sp, len);
	}

	free(sp);
	return 0;
}

/*
 * lines -- read lines to an offset from the end and display.
 *
 * This is the function that reads to a line offset from the end of the input,
 * storing the data in an array of buffers which is then displayed.  If the
 * rflag is set, the data is displayed in lines in reverse order, and this
 * routine has the usual nastiness of trying to find the newlines.  Otherwise,
 * it is displayed from the line closest to the beginning of the input to
 * the end.
 */
int
lines(FILE *fp, const char *fn, off_t off)
{
	struct {
		int blen;
		u_int len;
		char *l;
	} *llines;
	int ch, rc;
	char *p, *sp;
	int blen, cnt, recno, wrap;

	if ((llines = calloc(off, sizeof(*llines))) == NULL)
		err(1, "calloc");
	p = sp = NULL;
	blen = cnt = recno = wrap = 0;
	rc = 0;

	while ((ch = getc(fp)) != EOF) {
		if (++cnt > blen) {
			if ((sp = realloc(sp, blen += 1024)) == NULL)
				err(1, "realloc");
			p = sp + cnt - 1;
		}
		*p++ = ch;
		if (ch == '\n') {
			if ((int)llines[recno].blen < cnt) {
				llines[recno].blen = cnt + 256;
				if ((llines[recno].l = realloc(llines[recno].l,
				    llines[recno].blen)) == NULL)
					err(1, "realloc");
			}
			bcopy(sp, llines[recno].l, llines[recno].len = cnt);
			cnt = 0;
			p = sp;
			if (++recno == off) {
				wrap = 1;
				recno = 0;
			}
		}
	}
	if (ferror(fp)) {
		ierr(fn);
		rc = 1;
		goto done;
	}
	if (cnt) {
		llines[recno].l = sp;
		sp = NULL;
		llines[recno].len = cnt;
		if (++recno == off) {
			wrap = 1;
			recno = 0;
		}
	}

	if (rflag) {
		for (cnt = recno - 1; cnt >= 0; --cnt)
			WR(llines[cnt].l, llines[cnt].len);
		if (wrap)
			for (cnt = off - 1; cnt >= recno; --cnt)
				WR(llines[cnt].l, llines[cnt].len);
	} else {
		if (wrap)
			for (cnt = recno; cnt < off; ++cnt)
				WR(llines[cnt].l, llines[cnt].len);
		for (cnt = 0; cnt < recno; ++cnt)
			WR(llines[cnt].l, llines[cnt].len);
	}
done:
	for (cnt = 0; cnt < off; cnt++)
		free(llines[cnt].l);
	free(sp);
	free(llines);
	return (rc);
}
